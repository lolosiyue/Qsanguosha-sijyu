"""Private Google Sheets -> native worksheet IPC gateway (Python 3.11+).

Run behind an HTTPS reverse proxy/tunnel to this loopback listener. Every
player owns a separate native ClientCore process; no game rules live here.
"""
from __future__ import annotations

import argparse
import collections
import contextlib
import csv
import hashlib
import hmac
import http.client
import http.server
import io
import json
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import uuid

MAX_BODY = 1024 * 1024
MAX_REPLY = 4 * MAX_BODY
DECIMAL = re.compile(r"^(0|[1-9][0-9]{0,19})$")
COMMANDS = frozenset(("catalog", "connect", "host", "ready", "chat", "add_robot",
    "trust", "surrender", "reconnect", "select", "submit", "cancel", "details", "disconnect"))
IMAGE_TYPES = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif"}


class RequestError(Exception):
    def __init__(self, status: int, code: str):
        self.status, self.code = status, code
        super().__init__(code)


def compact(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")


def parse_object(raw: bytes) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate key")
            result[key] = value
        return result
    try:
        value = json.loads(raw, object_pairs_hook=unique,
            parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite")))
        if not isinstance(value, dict):
            raise ValueError("object required")
        return value
    except (ValueError, UnicodeError, RecursionError):
        raise RequestError(400, "invalid_json") from None


def decimal(value: object) -> bool:
    return isinstance(value, str) and DECIMAL.fullmatch(value) is not None and int(value) <= 2**64 - 1


def private_directory(path: Path) -> None:
    # Create a fresh directory; never change ACLs on somebody else's directory.
    path.mkdir(parents=False, exist_ok=False, mode=0o700)
    if os.name == "nt":
        system = Path(os.environ["SystemRoot"]) / "System32"
        output = subprocess.check_output([str(system / "whoami.exe"), "/user", "/fo", "csv", "/nh"],
            creationflags=subprocess.CREATE_NO_WINDOW, text=True)
        sid = next(csv.reader(io.StringIO(output)))[1]
        if not re.fullmatch(r"S-1-[0-9-]+", sid):
            raise RuntimeError("user_sid_unavailable")
        subprocess.run([str(system / "icacls.exe"), str(path), "/inheritance:r", "/grant:r", f"*{sid}:(OI)(CI)F"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW)


class NativeSession:
    def __init__(self, executable: Path, asset_root: Path, directory: Path, startup_timeout: float):
        self.session = str(uuid.uuid4())
        self.native_token = secrets.token_hex(32)
        self.token = secrets.token_urlsafe(32)
        self.code = secrets.token_urlsafe(18)
        self.pair_nonce = None
        self.created = self.last_seen = time.monotonic()
        self.paired_at = None
        self.lock = threading.RLock()
        self.closed = False
        self.exit_code = None
        self.cleanup_error = None
        self.forced_termination = False
        self.assets: dict[str, Path] = {}
        self.image_root = (asset_root / "image").resolve(strict=True)
        self.directory = directory
        private_directory(directory)
        ready = directory / "ready.json"
        environment = dict(os.environ, QSAN_SESSION_SETTINGS=str(directory / "config.ini"),
            QSAN_USER_DATA_ROOT=str(directory / "data"))
        self.process = None
        self.port = None
        try:
            with (directory / "native.log").open("wb") as diagnostic:
                self.process = subprocess.Popen([str(executable), "--asset-root", str(asset_root),
                    "--ready-file", str(ready)], stdin=subprocess.PIPE, stdout=diagnostic,
                    stderr=subprocess.STDOUT, cwd=str(asset_root), env=environment,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            # Only the inherited private pipe carries the native credential.
            self.process.stdin.write(compact({"session": self.session, "token": self.native_token}) + b"\n")
            self.process.stdin.flush()
            until = time.monotonic() + startup_timeout
            while time.monotonic() < until:
                if self.process.poll() is not None:
                    raise RuntimeError("native_start_failed")
                if ready.exists():
                    if ready.stat().st_size > 16384:
                        raise RuntimeError("invalid_native_ready")
                    value = parse_object(ready.read_bytes())
                    if (value.get("api_version") != 1 or value.get("session") != self.session
                            or value.get("status") != "ready" or value.get("pid") != self.process.pid
                            or type(value.get("port")) is not int or not 1 <= value["port"] <= 65535):
                        raise RuntimeError("invalid_native_ready")
                    self.port = value["port"]
                    self.created = self.last_seen = time.monotonic()
                    return
                time.sleep(0.1)
            raise RuntimeError("native_start_timeout")
        except BaseException:
            self.close()
            raise

    def request(self, method: str, path: str, body: dict | None = None) -> tuple[int, dict]:
        if self.closed or self.process.poll() is not None or not self.port:
            raise RequestError(410, "native_session_closed")
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        try:
            headers = {"Authorization": "Bearer " + self.native_token, "X-QSan-Session": self.session,
                "Content-Type": "application/json", "Connection": "close"}
            connection.request(method, path, body=compact(body) if body is not None else None, headers=headers)
            response = connection.getresponse()
            raw = response.read(MAX_REPLY + 1)
            if len(raw) > MAX_REPLY:
                raise RequestError(502, "native_reply_too_large")
            if response.status not in (200, 400, 401, 403, 404, 409, 413, 429, 503):
                raise RequestError(502, "native_response_unknown")
            value = parse_object(raw)
            if value.get("session", self.session) != self.session:
                raise RequestError(502, "native_session_mismatch")
            return response.status, value
        except (OSError, http.client.HTTPException):
            # The command may already have run. Never manufacture a success or
            # a fresh id; the frontend retries the exact immutable envelope.
            raise RequestError(502, "native_result_unknown") from None
        finally:
            connection.close()

    def project(self, value: object, key: str = "") -> object:
        if isinstance(value, dict):
            return {k: self.project(v, k) for k, v in value.items()}
        if isinstance(value, list):
            return [self.project(v) for v in value]
        if not isinstance(value, str):
            return value
        if key == "image" or key.endswith("_image"):
            if not value:
                return ""
            try:
                path = Path(value).resolve(strict=True)
                path.relative_to(self.image_root)
                if not path.is_file() or path.suffix.lower() not in IMAGE_TYPES or path.stat().st_size > MAX_REPLY:
                    return ""
            except (ValueError, OSError):
                return ""
            identifier = hmac.new(self.native_token.encode(), str(path).encode(), hashlib.sha256).hexdigest()
            if len(self.assets) >= 32768 and identifier not in self.assets:
                return ""
            self.assets[identifier] = path
            return "/v1/assets/" + identifier
        # Native diagnostic text may include Lua source paths. Never export
        # host filesystem locations (including auxiliary audio payload paths).
        value = value.replace(str(self.image_root.parent), "[runtime]")
        return re.sub(r"(?i)(?:[a-z]:[\\/]|\\\\)[^\s\"<>|]*", "[host path]", value)

    def asset(self, identifier: str) -> tuple[str, bytes]:
        path = self.assets.get(identifier)
        if path is None:
            raise RequestError(404, "asset_not_visible")
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(self.image_root)
            if resolved != path or not path.is_file() or path.stat().st_size > MAX_REPLY:
                raise ValueError("asset_changed")
            with path.open("rb") as stream:
                data = stream.read(MAX_REPLY + 1)
            if len(data) > MAX_REPLY:
                raise ValueError("asset_too_large")
            return IMAGE_TYPES[path.suffix.lower()], data
        except (OSError, ValueError):
            raise RequestError(404, "asset_unavailable") from None

    def close(self) -> None:
        with self.lock:
            if self.closed and (self.process is None or self.process.poll() is not None):
                return
            try:
                if self.process is not None:
                    acknowledged = False
                    if self.port and self.process.poll() is None:
                        with contextlib.suppress(RequestError):
                            status, reply = self.request("POST", "/v1/shutdown", {})
                            acknowledged = status == 200 and reply.get("ok") is True
                    if acknowledged:
                        # Keep the owner pipe alive during normal teardown, so
                        # the native process can distinguish it from owner loss.
                        with contextlib.suppress(subprocess.TimeoutExpired):
                            self.exit_code = self.process.wait(timeout=15)
                    if self.process.stdin:
                        with contextlib.suppress(OSError):
                            self.process.stdin.close()
                    try:
                        self.exit_code = self.process.wait(timeout=5 if acknowledged else 15)
                    except subprocess.TimeoutExpired:
                        self.forced_termination = True
                        self.process.kill()
                        self.exit_code = self.process.wait(timeout=5)
            except (OSError, subprocess.SubprocessError):
                self.cleanup_error = "native_stop_failed"
            finally:
                self.assets.clear()
                self.closed = True
            # A full disk must not strand other players' native processes.
            try:
                (self.directory / "exit.json").write_bytes(compact({"session": self.session,
                    "native_exit_code": self.exit_code, "clean_native_exit": self.clean_exit,
                    "forced_termination": self.forced_termination, "cleanup_error": self.cleanup_error,
                    "full_game_acceptance": "NOT_RUN"}))
            except OSError:
                self.cleanup_error = self.cleanup_error or "exit_evidence_write_failed"

    @property
    def clean_exit(self) -> bool:
        return self.exit_code == 0 and not self.forced_termination and self.cleanup_error is None


class Gateway:
    def __init__(self, sessions: list[NativeSession], *, pair_ttl=300, idle_timeout=1800,
                 allow_games=frozenset({("127.0.0.1", 9527)})):
        self.sessions = sessions
        self.pair_ttl, self.idle_timeout = pair_ttl, idle_timeout
        self.allow_games = allow_games
        self.lock = threading.RLock()
        self.pair_attempts = collections.deque()
        self.request_times = {s.session: collections.deque() for s in sessions}

    def pair(self, body: dict) -> dict:
        code, nonce = body.get("code"), body.get("pair_nonce")
        if not isinstance(code, str) or not isinstance(nonce, str) or not re.fullmatch(r"[A-Za-z0-9_-]{16,128}", nonce):
            raise RequestError(400, "invalid_pair_request")
        with self.lock:
            now = time.monotonic()
            while self.pair_attempts and now - self.pair_attempts[0] >= 60:
                self.pair_attempts.popleft()
            if len(self.pair_attempts) >= 30:
                raise RequestError(429, "pair_rate_limited")
            self.pair_attempts.append(now)
            for session in self.sessions:
                if not hmac.compare_digest(code.encode(), session.code.encode()):
                    continue
                if session.closed or now - session.created > self.pair_ttl:
                    break
                if session.pair_nonce is not None and session.pair_nonce != nonce:
                    break
                # Atomic, one owner; a lost response can be recovered only by
                # the original high-entropy nonce, within the pairing window.
                session.pair_nonce = nonce
                session.paired_at = session.paired_at or now
                session.last_seen = now
                return {"api_version": 1, "session": session.session, "token": session.token}
            raise RequestError(403, "pair_code_invalid_or_expired")

    def authenticate(self, authorization: str, identity: str, *, allow_closed=False) -> NativeSession:
        with self.lock:
            now = time.monotonic()
            for session in self.sessions:
                if not hmac.compare_digest(authorization.encode(), ("Bearer " + session.token).encode()):
                    continue
                if session.pair_nonce is None or identity != session.session:
                    break
                if not allow_closed and (session.closed or now - session.last_seen > self.idle_timeout):
                    raise RequestError(410, "session_expired")
                queue = self.request_times[session.session]
                while queue and now - queue[0] >= 60:
                    queue.popleft()
                if len(queue) >= 180:
                    raise RequestError(429, "session_rate_limited")
                queue.append(now)
                session.last_seen = now
                return session
            raise RequestError(401, "unauthorized")

    def validate_command(self, session: NativeSession, body: dict) -> None:
        if (type(body.get("api_version")) is not int or body["api_version"] != 1
                or body.get("session") != session.session or body.get("name") not in COMMANDS
                or not all(decimal(body.get(k)) for k in ("id", "generation", "revision"))
                or body.get("id") == "0" or not isinstance(body.get("args"), dict)):
            raise RequestError(400, "invalid_command_envelope")
        args = body["args"]
        if body["name"] in ("select", "submit", "cancel") and not decimal(args.get("request_id")):
            raise RequestError(400, "invalid_request_identity")
        if body["name"] == "connect":
            host, port = args.get("host"), args.get("port")
            if not isinstance(host, str) or type(port) is not int or (host, port) not in self.allow_games:
                raise RequestError(403, "game_endpoint_not_allowed")
        if body["name"] == "host" and args.get("private", True) is not True:
            # Pairing grants a player session, not arbitrary network listeners.
            # Multiplayer uses a host-approved existing game endpoint.
            raise RequestError(403, "use_approved_multiplayer_server")

    def sweep(self) -> None:
        now = time.monotonic()
        for session in self.sessions:
            with session.lock:
                # Recheck after acquiring the per-session lock; a concurrent
                # successful request can renew the reconnection window.
                expires = (session.pair_nonce is None and now - session.created > self.pair_ttl
                    or session.pair_nonce is not None and now - session.last_seen > self.idle_timeout)
                if not session.closed and (expires or session.process.poll() is not None):
                    session.close()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "QSanSheets"
    sys_version = ""

    def setup(self):
        super().setup()
        self.connection.settimeout(20)
        # Idle timeout alone lets slow uploads retain all worker slots forever.
        self.deadline = threading.Timer(30, self.expire_request)
        self.deadline.daemon = True
        self.deadline.start()

    def expire_request(self):
        with contextlib.suppress(OSError):
            self.connection.shutdown(socket.SHUT_RDWR)

    def finish(self):
        self.deadline.cancel()
        super().finish()

    def parse_request(self):
        if not super().parse_request():
            return False
        if sum(len(k) + len(v) + 4 for k, v in self.headers.items()) > 16384:
            self.reply(431, {"error": "headers_too_large"})
            return False
        return True

    def log_message(self, *_):
        pass  # Never log tokens, pairing codes, command bodies or private cards.

    def reply(self, status: int, value: object, content_type="application/json; charset=utf-8"):
        raw = value if isinstance(value, bytes) else compact(value)
        if len(raw) > MAX_REPLY:
            status, raw, content_type = 502, compact({"error": "reply_too_large"}), "application/json"
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store, private")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(raw)
        self.close_connection = True

    def read_body(self) -> dict:
        lengths = self.headers.get_all("Content-Length", [])
        if (self.headers.get_all("Transfer-Encoding") or len(lengths) != 1
                or len(lengths[0]) > 10 or not lengths[0].isdigit()):
            raise RequestError(400, "invalid_body_framing")
        length = int(lengths[0])
        if length > MAX_BODY:
            raise RequestError(413, "request_too_large")
        if self.headers.get("Content-Type", "").split(";")[0].strip().lower() != "application/json":
            raise RequestError(415, "json_required")
        raw = self.rfile.read(length)
        if len(raw) != length:
            raise RequestError(400, "incomplete_body")
        return parse_object(raw)

    def dispatch(self):
        try:
            if len(self.path) > 256 or self.path.startswith("//"):
                raise RequestError(400, "invalid_path")
            url = urllib.parse.urlsplit(self.path)
            if url.scheme or url.netloc or url.fragment:
                raise RequestError(400, "invalid_path")
            gateway = self.server.gateway
            if self.command == "POST" and url.path == "/v1/pair" and not url.query:
                self.reply(200, gateway.pair(self.read_body()))
                return
            if len(self.headers.get_all("Authorization", [])) != 1 or len(self.headers.get_all("X-QSan-Session", [])) != 1:
                raise RequestError(401, "unauthorized")
            shutdown = self.command == "POST" and url.path == "/v1/shutdown" and not url.query
            session = gateway.authenticate(self.headers["Authorization"], self.headers["X-QSan-Session"], allow_closed=shutdown)
            # Never wait for an upload while owning the native session lock.
            body = self.read_body() if self.command == "POST" else None
            with session.lock:
                if session.closed and not shutdown:
                    raise RequestError(410, "session_expired")
                if self.command == "GET" and re.fullmatch(r"/v1/assets/[0-9a-f]{64}", url.path) and not url.query:
                    kind, raw = session.asset(url.path.rsplit("/", 1)[1])
                    self.reply(200, raw, kind)
                elif self.command == "GET" and url.path == "/v1/updates":
                    query = urllib.parse.parse_qs(url.query, keep_blank_values=True)
                    if set(query) != {"after"} or len(query["after"]) != 1 or not decimal(query["after"][0]):
                        raise RequestError(400, "invalid_sequence")
                    status, result = session.request("GET", "/v1/updates?after=" + query["after"][0])
                    self.reply(status, session.project(result))
                elif self.command == "POST" and url.path == "/v1/commands" and not url.query:
                    gateway.validate_command(session, body)
                    status, result = session.request("POST", url.path, body)
                    self.reply(status, session.project(result))
                elif self.command == "POST" and url.path == "/v1/shutdown" and not url.query:
                    if body:
                        raise RequestError(400, "empty_shutdown_body_required")
                    session.close()
                    self.reply(200, {"api_version": 1, "session": session.session,
                        "ok": session.clean_exit, "native_exit_code": session.exit_code,
                        # A terminal failure is still a closed session. Clients
                        # may release its credentials without reporting success.
                        "closed": session.closed and (session.process is None or session.process.poll() is not None),
                        "cleanup_error": session.cleanup_error, "forced_termination": session.forced_termination})
                else:
                    raise RequestError(404, "unknown_endpoint")
        except RequestError as error:
            self.reply(error.status, {"error": error.code})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            self.close_connection = True
        except Exception:
            self.reply(500, {"error": "gateway_error"})

    do_GET = dispatch
    do_POST = dispatch


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, gateway):
        self.gateway = gateway
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


def endpoint(text: str) -> tuple[str, int]:
    try:
        host, port = text.rsplit(":", 1)
        port = int(port)
        if not host or not 1 <= port <= 65535:
            raise ValueError()
        return host, port
    except ValueError:
        raise argparse.ArgumentTypeError("expected HOST:PORT") from None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--asset-root", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path, help="Existing parent for fresh private run directories")
    parser.add_argument("--port", type=int, default=8766, help="Loopback HTTP port; expose only through HTTPS")
    parser.add_argument("--players", type=int, default=1, help="Prestarted isolated player slots (1-8)")
    parser.add_argument("--allow-game", action="append", type=endpoint, default=[])
    parser.add_argument("--pair-ttl", type=int, default=300)
    parser.add_argument("--idle-timeout", type=int, default=1800, help="Reconnect window in seconds")
    parser.add_argument("--startup-timeout", type=int, default=180)
    args = parser.parse_args()
    if (not 1 <= args.players <= 8 or not 1 <= args.port <= 65535 or not 60 <= args.pair_ttl <= 1800
            or not 60 <= args.idle_timeout <= 86400 or not 1 <= args.startup_timeout <= 600):
        parser.error("invalid limits")
    bridge = args.bridge.resolve(strict=True)
    assets = args.asset_root.resolve(strict=True)
    if not bridge.is_file() or not assets.is_dir():
        parser.error("invalid native runtime")
    run_root = args.state_root.resolve(strict=True) / str(uuid.uuid4())
    private_directory(run_root)
    sessions = []
    stopping = threading.Event()
    server = None
    sweeper = None
    def sweep():
        while not stopping.wait(5):
            gateway.sweep()
    try:
        # Bind before launching expensive processes; no credentials in argv.
        gateway = Gateway(sessions, pair_ttl=args.pair_ttl, idle_timeout=args.idle_timeout,
            allow_games=frozenset(args.allow_game or [("127.0.0.1", 9527)]))
        server = Server(("127.0.0.1", args.port), gateway)
        for index in range(args.players):
            session = NativeSession(bridge, assets, run_root / str(uuid.uuid4()), args.startup_timeout)
            sessions.append(session)
            gateway.request_times[session.session] = collections.deque()
        # Earlier slots get the full advertised pairing window too.
        for index, session in enumerate(sessions):
            session.created = session.last_seen = time.monotonic()
            print(f"Player {index + 1} pairing code (valid {args.pair_ttl}s): {session.code}", flush=True)
        print(f"HTTPS tunnel target: http://127.0.0.1:{args.port}; private diagnostics: {run_root}", flush=True)
        signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
        sweeper = threading.Thread(target=sweep, daemon=True)
        sweeper.start()
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    except Exception:
        print("Gateway startup failed; inspect the private native.log files.", file=sys.stderr)
        return 6
    finally:
        stopping.set()
        if server:
            server.server_close()
        if sweeper:
            sweeper.join(timeout=1)
        for session in sessions:
            session.close()
    return 0 if all(s.clean_exit for s in sessions) else 86


if __name__ == "__main__":
    raise SystemExit(main())
