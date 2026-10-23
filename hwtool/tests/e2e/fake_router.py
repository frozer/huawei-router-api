"""Stdlib-only fake Huawei LTE router for end-to-end tests of hwtool."""

import base64
import hashlib
import http.server
import re
import secrets
import threading
import time
import xml.etree.ElementTree as ET

XML_DECL = '<?xml version="1.0" encoding="UTF-8"?>\n'
TOKEN_HEADER = "__RequestVerificationToken"
TOKEN_ONE = "__RequestVerificationTokenOne"
TOKEN_TWO = "__RequestVerificationTokenTwo"
JUNK_PREFIX = 32

ERR_TOKEN = 125001
ERR_SESSION = 125002
ERR_LOGIN_WRONG = 108006
ERR_FORMAT = 100005
ERR_NO_RIGHTS = 100003  # not a 12500x code, so the client does not retry


def _b64(data):
    return base64.b64encode(data).decode("ascii")


def _sha256_hex(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def expected_password(username, password, password_type, token):
    """Mirrors the client: password_type 4 hashes with the token, otherwise plain base64."""
    if password_type == 4:
        password_hash = _b64(_sha256_hex(password).encode("ascii"))
        return _b64(_sha256_hex(username + password_hash + token).encode("ascii"))
    return _b64(password.encode("utf-8"))


def _xml(body, extra=()):
    return 200, (XML_DECL + body).encode("utf-8"), list(extra), "text/xml"


def _error(code):
    return _xml(f"<error><code>{code}</code><message></message></error>")


class _Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    router = None

    def handle_error(self, request, client_address):
        pass  # clients that time out drop their connection; keep the test output clean


class _Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")

    def _dispatch(self, method):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        # The raw target is used (not urlsplit), so "//x" is seen as a path, not a host.
        path = self.path.split("?", 1)[0]
        router = self.server.router
        router.record(method, path, self.headers, raw)
        if router.delay_seconds:
            time.sleep(router.delay_seconds)
        if router.status_override is not None:
            status, body, extra, ctype = router.status_override, b"", [], "text/plain"
        elif path.startswith("//"):
            status, body, extra, ctype = 404, b"", [], "text/plain"
        else:
            status, body, extra, ctype = router.handle(method, path, self.headers, raw)
        self.send_response(status)
        for name, value in extra:
            self.send_header(name, value)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class FakeRouter:
    """Fake router on 127.0.0.1 with a configurable login and SMS behaviour."""

    def __init__(self):
        self.username = "admin"
        self.password = "secret"
        self.password_type = 4
        self.logged_in = False
        self.send_error_code = None
        self.fail_phones = ""
        self.use_meta_tokens = True
        self.redirect_home = False
        self.delay_seconds = 0
        self.status_override = None
        self.requests = []  # (method, path, headers dict, body str)
        self.sent = []      # {"phones": [...], "content": str, "length": int}
        self.issued_tokens = []
        self._valid_tokens = set()
        self._session_id = None
        self._status_polls = 0
        self._lock = threading.Lock()
        self._server = None
        self._thread = None

    @property
    def url(self):
        return f"http://127.0.0.1:{self._server.server_address[1]}"

    def start(self):
        self._server = _Server(("127.0.0.1", 0), _Handler)
        self._server.router = self
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def stop(self):
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None

    def record(self, method, path, headers, raw):
        with self._lock:
            self.requests.append((method, path, dict(headers.items()),
                                  raw.decode("utf-8", "replace")))

    def handle(self, method, path, headers, raw):
        """Returns (status, body bytes, extra headers, content type)."""
        with self._lock:
            if method == "GET" and path == "/html/home.html":
                if self.redirect_home:
                    # The 302 sets no cookie: the client must keep the one from index.html.
                    return 302, b"", [("Location", "/html/index.html")], "text/html"
                return self._home_page()
            if method == "GET" and path == "/html/index.html":
                return self._home_page()
            if method == "GET" and path == "/api/webserver/token":
                token = self._issue_token()
                junk = secrets.token_hex(JUNK_PREFIX // 2)
                return _xml(f"<response><token>{junk}{token}</token></response>")
            if method == "GET" and path == "/api/user/state-login":
                return self._state(headers)
            if method == "POST" and path == "/api/user/login":
                return self._login(headers, raw)
            if method == "POST" and path == "/api/sms/send-sms":
                return self._send_sms(headers, raw)
            if method == "GET" and path == "/api/sms/send-status":
                return self._send_status(headers)
            return 404, b"", [], "text/plain"

    def _issue_token(self):
        token = secrets.token_hex(16)
        self._valid_tokens.add(token)
        self.issued_tokens.append(token)
        return token

    def _new_session_cookie(self):
        self._session_id = secrets.token_hex(16)
        return f"SessionID={self._session_id}; path=/; HttpOnly"

    def _has_session(self, headers):
        match = re.search(r"(?:^|;\s*)SessionID=([^;]+)", headers.get("Cookie") or "")
        current = self._session_id
        return current is not None and match is not None and match.group(1) == current

    def _session_ok(self, headers):
        return self._has_session(headers) and self.logged_in

    def _take_token(self, headers):
        token = headers.get(TOKEN_HEADER)
        if token is None or token not in self._valid_tokens:
            return None
        self._valid_tokens.discard(token)
        return token

    def _home_page(self):
        extra = [("Set-Cookie", self._new_session_cookie())]
        metas = ""
        if self.use_meta_tokens:
            for token in (self._issue_token(), self._issue_token()):
                metas += f'<meta name="csrf_token" content="{token}"/>'
        html = f"<!DOCTYPE html><html><head><title>Home</title>{metas}</head><body></body></html>"
        return 200, html.encode("utf-8"), extra, "text/html"

    def _state(self, headers):
        if not self._has_session(headers):
            return _error(ERR_SESSION)
        state = 0 if self.logged_in else -1
        return _xml(f"<response><State>{state}</State><Username></Username>"
                    f"<password_type>{self.password_type}</password_type></response>")

    def _login(self, headers, raw):
        if not self._has_session(headers):
            return _error(ERR_SESSION)
        token = self._take_token(headers)
        if token is None:
            return _error(ERR_TOKEN)
        try:
            root = ET.fromstring(raw)
            username = root.findtext("Username", "")
            given = root.findtext("Password", "")
            password_type = int(root.findtext("password_type", "0"))
        except (ET.ParseError, ValueError):
            return _error(ERR_FORMAT)
        expected = expected_password(username, self.password, password_type, token)
        if username != self.username or given != expected:
            return _error(ERR_LOGIN_WRONG)
        self.logged_in = True
        cookie = self._new_session_cookie()
        one, two = self._issue_token(), self._issue_token()
        return _xml("<response>OK</response>",
                    [("Set-Cookie", cookie), (TOKEN_ONE, one), (TOKEN_TWO, two)])

    def _send_sms(self, headers, raw):
        if not self._session_ok(headers) or self._take_token(headers) is None:
            return _error(ERR_NO_RIGHTS)
        try:
            root = ET.fromstring(raw)
        except ET.ParseError:
            return _error(ERR_FORMAT)
        phones = [node.text or "" for node in root.findall("Phones/Phone")]
        self.sent.append({
            "phones": phones,
            "content": root.findtext("Content", ""),
            "length": int(root.findtext("Length", "-1")),
        })
        self._status_polls = 0
        if self.send_error_code is not None:
            return _error(self.send_error_code)
        return _xml("<response>OK</response>", [(TOKEN_HEADER, self._issue_token())])

    def _send_status(self, headers):
        if not self._session_ok(headers):
            return _error(ERR_NO_RIGHTS)
        self._status_polls += 1
        phones = self.sent[-1]["phones"] if self.sent else []
        total = len(phones)
        # The first poll after a send reports progress, later polls report completion.
        cur = total if self._status_polls > 1 else 0
        failed = set(re.split(r"[;,]", self.fail_phones)) - {""}
        succeeded = ";".join(p for p in phones if p not in failed)
        failed_list = ";".join(p for p in phones if p in failed)
        return _xml(f"<response><TotalCount>{total}</TotalCount><CurIndex>{cur}</CurIndex>"
                    f"<Phone></Phone><SucPhone>{succeeded}</SucPhone>"
                    f"<FailPhone>{failed_list}</FailPhone></response>")
