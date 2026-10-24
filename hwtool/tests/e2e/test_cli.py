"""End-to-end tests: run the real hwtool binary (HWTOOL_BIN) against the fake router."""

import json
import os
import re
import shlex
import socket
import subprocess
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
# python -I does not put the script directory on sys.path, so add it explicitly.
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from fake_router import FakeRouter  # noqa: E402

PHONE = "+48123456789"
MESSAGE = "Hello ąę \U0001F600"
PASSWORD = "secret"
SEND_PATHS = ["/html/home.html", "/api/user/state-login", "/api/user/login", "/api/sms/send-sms"]


class HwtoolTestCase(unittest.TestCase):
    def setUp(self):
        self.binary = os.environ.get("HWTOOL_BIN", "")
        self.assertTrue(self.binary, "HWTOOL_BIN must name the hwtool binary")
        # Set when cross-compiling, e.g. "qemu-mipsel", so the binary runs on the build host.
        self.emulator = shlex.split(os.environ.get("HWTOOL_EMULATOR", ""))
        self.router = FakeRouter()
        self.router.start()
        self.addCleanup(self.router.stop)

    def run_hwtool(self, args, url=None, env=None, timeout=20):
        """Runs hwtool with --url pointing at the fake router unless url is given."""
        clean_env = {
            k: v for k, v in os.environ.items()
            if k != "HWTOOL_PASSWORD" and not k.lower().endswith("_proxy")
        }
        clean_env["NO_PROXY"] = "127.0.0.1"
        clean_env["no_proxy"] = "127.0.0.1"
        if env:
            clean_env.update(env)
        argv = [*self.emulator, self.binary, "--url", url or self.router.url, *args]
        return subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                              env=clean_env, timeout=timeout)

    def paths(self):
        return [path for _, path, _, _ in self.router.requests]

    @staticmethod
    def send_args(*extra):
        return ["send-sms", "-t", PHONE, "-m", MESSAGE, *extra]


class SendSmsTest(HwtoolTestCase):
    def test_success_sends_message_and_reports(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"SMS sent to {PHONE}\n")
        self.assertEqual(len(self.router.sent), 1)
        sent = self.router.sent[0]
        self.assertEqual(sent["phones"], [PHONE])
        self.assertEqual(sent["content"], MESSAGE)
        self.assertEqual(sent["length"], len(MESSAGE.encode("utf-16-le")) // 2)
        self.assertEqual(self.paths(), SEND_PATHS)

    def test_password_before_subcommand(self):
        result = self.run_hwtool(["-p", PASSWORD, *self.send_args()])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.router.sent), 1)

    def test_password_from_environment(self):
        result = self.run_hwtool(self.send_args(), env={"HWTOOL_PASSWORD": PASSWORD})

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.router.sent), 1)

    def test_user_option_changes_login_username(self):
        self.router.username = "bob"

        result = self.run_hwtool(["-u", "bob", *self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        login = [body for _, path, _, body in self.router.requests
                 if path == "/api/user/login"]
        self.assertEqual(len(login), 1)
        self.assertIn("<Username>bob</Username>", login[0])

    def test_wrong_password_exits_4(self):
        result = self.run_hwtool([*self.send_args("-p", "wrong")])

        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertEqual(self.router.sent, [])

    def test_plain_password_type_zero_succeeds(self):
        self.router.password_type = 0

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.router.sent), 1)

    def test_already_logged_in_skips_login(self):
        self.router.logged_in = True

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("/api/user/login", self.paths())
        self.assertEqual(len(self.router.sent), 1)

    def test_redirect_on_home_page_is_followed_with_cookies(self):
        self.router.redirect_home = True

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("/html/index.html", self.paths())
        self.assertNotIn("/api/webserver/token", self.paths())
        self.assertEqual(len(self.router.sent), 1)

    def test_token_fallback_without_meta_tags(self):
        self.router.use_meta_tokens = False

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("/api/webserver/token", self.paths())
        self.assertEqual(len(self.router.sent), 1)

    def test_trailing_slash_in_url_is_trimmed(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD)], url=self.router.url + "/")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.router.sent), 1)

    def test_upper_case_scheme_is_accepted(self):
        url = self.router.url.replace("http://", "HTTP://")

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)], url=url)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.router.sent), 1)

    def test_closed_port_exits_3(self):
        # Bound but never listening: connections are refused.
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.addCleanup(probe.close)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)],
                                 url=f"http://127.0.0.1:{port}")

        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("[http_request_no_response]", result.stderr)

    def test_timeout_exits_3_quickly(self):
        self.router.delay_seconds = 3

        started = time.monotonic()
        result = self.run_hwtool(["--timeout", "0.5", *self.send_args("-p", PASSWORD)])
        elapsed = time.monotonic() - started

        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("[http_request_timeout]", result.stderr)
        self.assertLess(elapsed, 2.5)

    def test_http_500_exits_3(self):
        self.router.status_override = 500

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("[http_request_invalid_status]", result.stderr)

    def test_bad_url_exits_2_without_requests(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD)], url="ftp://x")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(self.router.requests, [])

    def test_url_without_host_exits_2_without_requests(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD)], url="http://")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(self.router.requests, [])

    def test_wait_delivered_exits_0(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD, "--wait")])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"SMS delivered to {PHONE}\n")

    def test_wait_failed_exits_6(self):
        self.router.fail_phones = PHONE

        result = self.run_hwtool([*self.send_args("-p", PASSWORD, "--wait")])

        self.assertEqual(result.returncode, 6, result.stderr)

    def test_wait_two_numbers_one_failed_names_it(self):
        self.router.fail_phones = "+2"

        result = self.run_hwtool(["send-sms", "-t", "+1,+2", "-m", MESSAGE, "-p", PASSWORD,
                                  "--wait"])

        self.assertEqual(result.returncode, 6, result.stderr)
        self.assertEqual(result.stdout, "SMS failed for +2\n")

    def test_wait_json_delivered_reports_status(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD, "--wait", "--json")])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["status"], "delivered")

    def test_json_output_parses(self):
        result = self.run_hwtool([*self.send_args("-p", PASSWORD, "--json")])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout),
                         {"status": "sent", "numbers": [PHONE]})

    def test_verbose_logs_requests_without_secrets(self):
        result = self.run_hwtool(["-v", *self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("hwtool: GET html/home.html -> 200", result.stderr)
        self.assertIn("hwtool: POST api/sms/send-sms -> 200", result.stderr)
        self.assertNotIn(PASSWORD, result.stderr)
        for token in self.router.issued_tokens:
            self.assertNotIn(token, result.stderr)
        logins = [body for _, path, _, body in self.router.requests
                  if path == "/api/user/login"]
        self.assertEqual(len(logins), 1)
        match = re.search(r"<Password>(.*?)</Password>", logins[0])
        self.assertIsNotNone(match)
        self.assertNotIn(match.group(1), result.stderr)

    def test_router_send_error_exits_5(self):
        self.router.send_error_code = 113004

        result = self.run_hwtool([*self.send_args("-p", PASSWORD)])

        self.assertEqual(result.returncode, 5, result.stderr)


if __name__ == "__main__":
    unittest.main()
