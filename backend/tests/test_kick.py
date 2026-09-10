"""Tests T-046: adapter Kick + flujo Connect con fake provider (sin red)."""
import threading
import unittest
import urllib.parse

from backend.adapters.kick import (BROWSER_UA, KickProvider, classify_kick_error,
                                   _post_form)
from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Provider
from backend.oauth import ConnectService
from backend.stores import (InMemoryConnectionStore, InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets


def make_service(transport, clock=None):
    import time as _time
    clock = clock or _time.time
    provider = KickProvider(FakeSecrets(), "http://localhost:3000/cb",
                            transport=transport)
    return (ConnectService(provider, InMemoryOAuthTransactionStore(clock=clock),
                           InMemoryConnectionStore(), InMemoryTokenStore(),
                           clock=clock), provider)


def fake_kick_ok(method, url, fields):
    if url.endswith("/token") and fields.get("grant_type") == "authorization_code":
        assert "client_secret" in fields, "el secret debe viajar al token endpoint"
        return 200, {"access_token": "kat-1", "refresh_token": "krt-1",
                     "expires_in": 7200, "scope": "channel:write channel:read"}
    if url.endswith("/token"):
        return 200, {"access_token": "kat-2", "refresh_token": "krt-2",
                     "expires_in": 7200, "scope": "s"}
    if "revoke" in url:
        return 200, {}
    if "channels" in url:
        return 200, {"data": [{"broadcaster_user_id": 99, "slug": "kick-test",
                               "stream_title": ""}]}
    raise AssertionError(url)


class FakeClock:
    def __init__(self, now: float = 3_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


class AdapterTest(unittest.TestCase):
    def test_error_mapping(self):
        self.assertEqual(classify_kick_error({"error": "invalid_grant"}, 400).code,
                         ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(classify_kick_error({"error": "invalid_client"}, 401).code,
                         ErrorCode.INTERNAL)
        self.assertEqual(classify_kick_error({"error": "access_denied"}, 400).code,
                         ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_kick_error({"error": "x"}, 429).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_kick_error({"error": "x"}, 500).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)
        self.assertEqual(classify_kick_error({"error": "code: 1010"}, 403).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)

    def test_exchange_missing_token_is_rejected(self):
        def transport(method, url, fields):
            return 400, {"error": "Invalid request"}
        provider = KickProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.PROVIDER_REJECTED)

    def test_refresh_keeps_old_token_when_absent(self):
        def transport(method, url, fields):
            return 200, {"access_token": "kat-new", "expires_in": 7200}
        provider = KickProvider(FakeSecrets(), "http://x/cb", transport=transport)
        out = provider.refresh(Account(Provider.KICK, "99", "N"), "krt-old")
        self.assertEqual((out.access_token, out.refresh_token),
                         ("kat-new", "krt-old"))

    def test_identity_empty_is_rejected(self):
        def transport(method, url, fields):
            return 200, {"data": []}
        provider = KickProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError):
            provider.fetch_identity("kat")

    def test_missing_credentials_is_internal(self):
        provider = KickProvider(FakeSecrets(missing=True), "http://x/cb")
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)

    def test_browser_user_agent_on_real_path(self):
        """Cloudflare (F-022): el path real urllib debe llevar UA de navegador."""
        import urllib.request
        import backend.adapters.kick as kick_mod
        captured = {}
        real_urlopen = urllib.request.urlopen

        class FakeResponse:
            status = 200

            def read(self):
                return b"{}"

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def spy(req, timeout=None):
            headers = {k.lower(): v for k, v in req.header_items()}
            captured["User-Agent"] = headers.get("user-agent")
            return FakeResponse()

        urllib.request.urlopen = spy
        try:
            kick_mod._post_form("https://id.kick.com/oauth/token", {"a": "b"})
        finally:
            urllib.request.urlopen = real_urlopen
        self.assertEqual(captured.get("User-Agent"), BROWSER_UA)


class FlowTest(unittest.TestCase):
    def _connected(self, clock=None):
        clock = clock or FakeClock()
        svc, _ = make_service(fake_kick_ok, clock=clock)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "auth-code-9")
        return svc, result

    def test_full_connect_maps_identity(self):
        svc, result = self._connected()
        self.assertEqual(result["status"], "connected")
        self.assertEqual(result["account"], {"id": "99",
                                             "displayName": "kick-test"})
        self.assertEqual(svc.connection("inst-9").account.provider,
                         Provider.KICK)

    def test_authorization_url_shape_localhost(self):
        svc, _ = make_service(fake_kick_ok)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        url = started["authorization_url"]
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(url).query))
        self.assertTrue(url.startswith("https://id.kick.com/oauth/authorize?"))
        self.assertIn("channel:write", q["scope"])
        self.assertEqual(q["code_challenge_method"], "S256")
        self.assertNotIn("client_secret", url)
        self.assertIn("localhost", q["redirect_uri"])
        self.assertNotIn("127.0.0.1", started["authorization_url"])

    def test_callback_replay_and_bad_state_rejected(self):
        clock = FakeClock()
        svc, _ = make_service(fake_kick_ok, clock=clock)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback("state-inexistente", "code")

    def test_expired_and_denied(self):
        clock = FakeClock()
        svc, _ = make_service(fake_kick_ok, clock=clock)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        clock.now += 601
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code")
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        with self.assertRaises(AppError) as ctx:
            svc.callback(txn["state"], "", error="access_denied")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHORIZATION)

    def test_wrong_provider_and_installation_binding(self):
        from backend.adapters.youtube import YouTubeProvider
        from backend.oauth import ConnectService
        clock = FakeClock()
        svc, _ = make_service(fake_kick_ok, clock=clock)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        # Transacción Kick en servicio YouTube → REJECT.
        yt = ConnectService(
            YouTubeProvider(FakeSecrets(), "http://127.0.0.1:9004/cb"),
            svc._transactions, svc._connections, svc._tokens, clock=clock)
        with self.assertRaises(AppError):
            yt.callback(txn["state"], "code-1")
        self.assertEqual(svc.status("inst-otra")["status"], "disconnected")

    def test_refresh_and_disconnect(self):
        clock = FakeClock()
        revoked = []

        def transport(method, url, fields):
            if "revoke" in url:
                revoked.append(True)
                return 200, {}
            return fake_kick_ok(method, url, fields)

        svc, _ = make_service(transport, clock=clock)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        clock.now += 7200
        self.assertEqual(svc.ensure_fresh_token("inst-9"), "kat-2")
        svc.disconnect("inst-9")
        self.assertEqual(svc.status("inst-9")["status"], "disconnected")
        self.assertTrue(revoked)


if __name__ == "__main__":
    unittest.main()
