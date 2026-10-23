# hwtool

`hwtool` is a command-line tool for Huawei LTE routers. It logs in to the router's web API and
sends SMS messages, optionally waiting for the router to report delivery.

## Requirements

- CMake 3.16 or newer, Ninja (or another CMake generator), git (CMake fetches dependencies)
- A C++20 compiler (GCC 9+ or Clang 12+)
- libcurl development headers (7.68 or newer)
- Python 3.8 or newer (only for the end-to-end tests)

On Debian/Ubuntu (the stock packages of Ubuntu 20.04 and later are sufficient):

```sh
sudo apt install build-essential cmake ninja-build git libcurl4-openssl-dev python3
```

pugixml, CLI11 and Catch2 are downloaded by CMake at configure time.

## Build and test

```sh
cmake -S hwtool -B hwtool/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHWTOOL_WERROR=ON
cmake --build hwtool/build
(cd hwtool/build && ctest --output-on-failure)
```

Build options: `HWTOOL_BUILD_TESTS` (default ON) builds the tests, `HWTOOL_WERROR` (default OFF)
turns warnings into errors, and `HWTOOL_STATIC` (default OFF) links libstdc++ and libgcc statically.

## Usage

```sh
hwtool send-sms -t <phone> -m <message> -p <password>
```

Examples:

```sh
# Several recipients: repeat -t or separate numbers with commas
hwtool send-sms -t +48123456789 -t +48987654321 -m "Hello" -p secret
hwtool send-sms -t "+48123456789,+48987654321" -m "Hello" -p secret

# Wait for the router to report delivery (exit 6 if any number failed)
hwtool send-sms -t +48123456789 -m "Hello" -p secret --wait --wait-timeout 60

# Machine-readable output
hwtool send-sms -t +48123456789 -m "Hello" -p secret --json

# Log each HTTP request to stderr (no passwords or tokens are logged)
hwtool send-sms -t +48123456789 -m "Hello" -p secret -v

# Other router, user and timeout
hwtool --url http://192.168.8.1 -u admin --timeout 5 send-sms -t +48123456789 -m "Hi" -p secret
```

Global options may appear before or after the subcommand.

### send-sms options

| Option | Meaning |
| --- | --- |
| `-t, --to <phone>` | Recipient. Repeatable, and each value may hold comma-separated numbers. Required. |
| `-m, --message <text>` | Message text (UTF-8, not empty). Required. |
| `--wait` | After sending, poll the router until delivery is reported. |
| `--wait-timeout <seconds>` | Limit for `--wait` (default 30, finite and > 0). |

### Global options

| Option | Default | Meaning |
| --- | --- | --- |
| `--url <url>` | `http://192.168.8.1` | Router base URL. Must start with `http://` or `https://`. |
| `-u, --user <name>` | `admin` | Router user name. |
| `-p, --password <pw>` | `HWTOOL_PASSWORD` | Router password. Required for commands that log in. |
| `--timeout <seconds>` | `10` | Timeout for each HTTP request (finite and > 0). |
| `-v, --verbose` | off | Log each HTTP request to stderr. |
| `--json` | off | Print results and errors as one-line JSON. |
| `--version` | | Print the version and exit. |

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success (for `--wait`, the message was delivered). |
| 1 | Internal error. |
| 2 | Usage error: bad arguments, missing password, bad URL, invalid message or number. |
| 3 | Network error: no connection, timeout, or HTTP error status. |
| 4 | Authentication failed (wrong user name or password). |
| 5 | Router error, including a `--wait` timeout. |
| 6 | Partial or failed delivery (`--wait` only). |

### Security note

`-p` puts the password in the process list (visible to `ps`) and often in shell history. Prefer
the environment variable:

```sh
export HWTOOL_PASSWORD='secret'
hwtool send-sms -t +48123456789 -m "Hello"
```

## Architecture

- `hwapi` (`include/hw/`, `lib/`): a library with no command-line code. It holds the libcurl
  transport (`CurlTransport`), the session with CSRF token handling, the XML helpers, and the
  admin and SMS API calls (`hw::api::Admin`, `hw::api::Sms`).
- `hwtool_app` (`app/`, except `main.cpp`): the command-line framework on top of `hwapi`. It has
  the output and exit-code rules, the global options and CLI11 wiring (`cli.cpp`), the `Context`
  that creates the session lazily, and the `Command` interface and `CommandRegistry`. The
  commands live in `app/commands/`.
- `hwtool` (`app/main.cpp`): wires the registry, the libcurl transport factory and `run_cli`.

### Adding a command

1. Write a class that derives from `hwtool::Command` in `app/commands/`. Implement `configure`
   (declare its options on the CLI11 subcommand) and `run` (return the exit code).
2. If the command needs router calls, add them to `include/hw/api/` and `lib/api/` in `hwapi`,
   with unit tests in `hwapi_tests`.
3. Register the command in `app/main.cpp` with one line:
   `registry.add<MyCommand>("my-command", "Description");`
