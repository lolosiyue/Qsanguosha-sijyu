"""Focused gateway contracts; run explicitly after the source checkpoint approval."""
import importlib.util
from pathlib import Path
import tempfile
import threading
import time
import unittest

spec = importlib.util.spec_from_file_location("sheets_gateway", Path(__file__).parents[1] / "gateway.py")
gateway = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gateway)


class Session:
    def __init__(self, identity, token, code):
        self.session, self.token, self.code = identity, token, code
        self.pair_nonce = self.paired_at = None
        self.created = self.last_seen = time.monotonic()
        self.closed = False


class PairingTests(unittest.TestCase):
    def setUp(self):
        self.a = Session("a", "token-a", "code-a")
        self.b = Session("b", "token-b", "code-b")
        self.service = gateway.Gateway([self.a, self.b])

    def pair(self, code="code-a", nonce="nonce-0123456789abcdef"):
        return self.service.pair({"code": code, "pair_nonce": nonce})

    def test_lost_pair_reply_same_nonce_recoverable(self):
        self.assertEqual(self.pair(), self.pair())
        with self.assertRaises(gateway.RequestError):
            self.pair(nonce="other-0123456789abcdef")

    def test_concurrent_redemption_has_one_owner(self):
        barrier = threading.Barrier(2)
        results = []
        def attempt(nonce):
            barrier.wait()
            try:
                results.append(self.pair(nonce=nonce))
            except gateway.RequestError:
                results.append(None)
        threads = [threading.Thread(target=attempt, args=("nonce-" + str(i) * 20,)) for i in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(2)
            self.assertFalse(thread.is_alive())
        self.assertEqual(sum(result is not None for result in results), 1)

    def test_identity_is_not_authorization(self):
        self.pair()
        self.pair("code-b")
        with self.assertRaises(gateway.RequestError):
            self.service.authenticate("Bearer token-a", "b")
        self.assertIs(self.service.authenticate("Bearer token-a", "a"), self.a)

    def test_unpaired_and_expired_sessions_rejected(self):
        with self.assertRaises(gateway.RequestError):
            self.service.authenticate("Bearer token-a", "a")
        self.a.created -= 301
        with self.assertRaises(gateway.RequestError):
            self.pair()
        self.a.created = time.monotonic()
        self.pair()
        self.a.last_seen -= 1801
        with self.assertRaises(gateway.RequestError):
            self.service.authenticate("Bearer token-a", "a")

    def test_pair_rate_limit_bounds_global_attempts(self):
        for _ in range(30):
            with self.assertRaises(gateway.RequestError):
                self.pair("wrong")
        with self.assertRaises(gateway.RequestError) as raised:
            self.pair()
        self.assertEqual(raised.exception.status, 429)

    def test_shutdown_retry_can_read_closed_session_only_with_original_credentials(self):
        self.pair()
        self.a.closed = True
        with self.assertRaises(gateway.RequestError):
            self.service.authenticate("Bearer token-a", "a")
        self.assertIs(self.service.authenticate("Bearer token-a", "a", allow_closed=True), self.a)
        with self.assertRaises(gateway.RequestError):
            self.service.authenticate("Bearer token-a", "b", allow_closed=True)


class EnvelopeTests(unittest.TestCase):
    def setUp(self):
        self.session = Session("a", "token", "code")
        self.service = gateway.Gateway([self.session])
        self.body = {"api_version": 1, "session": "a", "id": "9007199254740993",
            "generation": "2", "revision": "3", "name": "submit",
            "args": {"request_id": "9007199254740995", "draft": {"cards": [1], "targets": ["p2", "p2"]}}}

    def test_large_string_ids_and_ordered_repeated_targets_unchanged(self):
        before = gateway.compact(self.body)
        self.service.validate_command(self.session, self.body)
        self.assertEqual(gateway.compact(self.body), before)

    def test_numeric_or_overflow_ids_fail(self):
        for value in (1, 1.0, True, "01", "-1", str(2**64)):
            self.body["id"] = value
            with self.assertRaises(gateway.RequestError):
                self.service.validate_command(self.session, self.body)

    def test_arbitrary_network_destination_not_allowed(self):
        self.body.update(name="connect", args={"host": "169.254.169.254", "port": 80})
        with self.assertRaises(gateway.RequestError):
            self.service.validate_command(self.session, self.body)
        self.body["args"] = {"host": "127.0.0.1", "port": 9527}
        self.service.validate_command(self.session, self.body)

    def test_pairing_cannot_create_public_listener(self):
        self.body.update(name="host", args={"private": False})
        with self.assertRaises(gateway.RequestError):
            self.service.validate_command(self.session, self.body)

    def test_duplicate_json_and_nonfinite_fail(self):
        for raw in (b'{"session":"a","session":"b"}', b'{"id":NaN}', b'[]'):
            with self.assertRaises(gateway.RequestError):
                gateway.parse_object(raw)


class ProjectionTests(unittest.TestCase):
    def test_only_emitted_images_receive_session_local_handles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "image"
            image.mkdir()
            visible = image / "visible.png"
            visible.write_bytes(b"png-example")
            secret = root / "private.png"
            secret.write_bytes(b"private")
            session = object.__new__(gateway.NativeSession)
            session.image_root, session.native_token, session.assets = image.resolve(), "a" * 64, {}
            projected = session.project({"image": str(visible), "general_image": str(secret),
                "detail": "Lua failed at C:\\runtime\\private\\file.lua"})
            self.assertTrue(projected["image"].startswith("/v1/assets/"))
            self.assertEqual(projected["general_image"], "")
            self.assertNotIn("C:\\", projected["detail"])
            handle = projected["image"].rsplit("/", 1)[1]
            self.assertEqual(session.asset(handle), ("image/png", b"png-example"))
            with self.assertRaises(gateway.RequestError):
                session.asset("0" * 64)
            other = object.__new__(gateway.NativeSession)
            other.image_root, other.native_token, other.assets = image.resolve(), "b" * 64, {}
            with self.assertRaises(gateway.RequestError):
                other.asset(handle)


if __name__ == "__main__":
    unittest.main()
