"""Tests FASE 2: adapter Twitch Managed (auth-code) + flujo Connect (sin red)."""
import unittest
import urllib.parse

from backend.adapters.twitch import (TwitchProvider, classify_twitch_error,
                                     _scope_text)
from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Provider
from backend.oauth import ConnectService
from backend.stores import (InMemoryConnectionStore, InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets


def make_service(transport, clock=None):
    import time as _time
    clock = clock or _time.time
    provider = TwitchProvider(FakeSecrets(), "http://127.0.0.1:8080/cb",
                              transport=transport)
    return (ConnectService(provider, InMemoryOAuthTransactionStore(clock=clock),
                           InMemoryConnectionStore(), InMemoryTokenStore(),
                           clock=clock), provider)


def fake_twitch_ok(method, url, fields):
    if url.endswith("/token") and fields.get("grant_type") == "authorization_code":
        assert "client_secret" in fields, "el secret debe viajar al token endpoint"
        assert "code_verifier" not in fields, "Twitch code flow no usa PKCE"
        return 200, {"access_token": "twat-1", "refresh_token": "twrt-1",
                     "expires_in": 14124, "scope": ["channel:manage:broadcast"],
                     "token_type": "bearer"}
    if url.endswith("/token"):
        return 200, {"access_token": "twat-2", "refresh_token": "twrt-2",
                     "expires_in": 14124, "scope": ["channel:manage:broadcast"]}
    if "revoke" in url:
        assert "client_id" in url, "revoke Twitch exige client_id en query"
        return 200, {}
    if "validate" in url:
        return 200, {"client_id": "test-tw-id", "login": "tw-test",
                     "scopes": ["channel:manage:broadcast"], "user_id": "18200",
                     "expires_in": 14000}
    raise AssertionError(url)


class FakeClock:
    def __init__(self, now: float = 3_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


class AdapterTest(unittest.TestCase):
    def test_error_mapping(self):
        self.assertEqual(classify_twitch_error(
            {"status": 400, "message": "invalid grant"}, 400).code,
            ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(classify_twitch_error(
            {"status": 400, "message": "Invalid refresh token"}, 400).code,
            ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(classify_twitch_error(
            {"status": 401, "message": "invalid client"}, 401).code,
            ErrorCode.INTERNAL)
        self.assertEqual(classify_twitch_error(
            {"error": "access_denied"}, 400).code, ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_twitch_error({"message": "x"}, 429).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_twitch_error({"message": "x"}, 500).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)

    def test_scope_list_normalized(self):
        self.assertEqual(_scope_text(["channel:manage:broadcast"]),
                         "channel:manage:broadcast")
        self.assertEqual(_scope_text("s"), "s")

    def test_exchange_missing_token_is_rejected(self):
        def transport(method, url, fields):
            return 400, {"status": 400, "message": "invalid grant"}
        provider = TwitchProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.PROVIDER_REJECTED)

    def test_refresh_keeps_old_token_when_absent(self):
        def transport(method, url, fields):
            return 200, {"access_token": "twat-new", "expires_in": 14124}
        provider = TwitchProvider(FakeSecrets(), "http://x/cb", transport=transport)
        out = provider.refresh(Account(Provider.TWITCH, "99", "N"), "twrt-old")
        self.assertEqual((out.access_token, out.refresh_token),
                         ("twat-new", "twrt-old"))

    def test_identity_empty_is_rejected(self):
        def transport(method, url, fields):
            return 200, {"user_id": "", "login": ""}
        provider = TwitchProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError):
            provider.fetch_identity("twat")

    def test_missing_credentials_is_internal(self):
        provider = TwitchProvider(FakeSecrets(missing=True), "http://x/cb")
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)

    def test_required_secret_names(self):
        self.assertEqual(TwitchProvider.required_secret_names,
                         ("TWITCH_CLIENT_ID", "TWITCH_CLIENT_SECRET"))

    def test_independent_stays_direct(self):
        self.assertTrue(TwitchProvider.DIRECT_FROM_PLUGIN)


class FlowTest(unittest.TestCase):
    def _connected(self, clock=None):
        clock = clock or FakeClock()
        svc, _ = make_service(fake_twitch_ok, clock=clock)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "auth-code-7")
        return svc, result

    def test_full_connect_maps_identity(self):
        svc, result = self._connected()
        self.assertEqual(result["status"], "connected")
        self.assertEqual(result["provider"], "twitch")
        self.assertEqual(result["account"], {"id": "18200",
                                             "displayName": "tw-test"})
        conn = svc.connection("inst-7")
        self.assertEqual(conn.account.provider, Provider.TWITCH)
        self.assertEqual(conn.account.scopes, ("channel:manage:broadcast",))

    def test_authorization_url_shape(self):
        svc, _ = make_service(fake_twitch_ok)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        url = started["authorization_url"]
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(url).query))
        self.assertTrue(url.startswith("https://id.twitch.tv/oauth2/authorize?"))
        self.assertEqual(q["response_type"], "code")
        self.assertEqual(q["scope"], "channel:manage:broadcast")
        self.assertTrue(q["state"])
        self.assertEqual(q["redirect_uri"], "http://127.0.0.1:8080/cb")
        # Twitch code flow: sin PKCE, sin secretos en la URL.
        self.assertNotIn("code_challenge", q)
        self.assertNotIn("code_challenge_method", q)
        self.assertNotIn("client_secret", url)

    def test_tokens_stored_securely_and_fresh(self):
        svc, _ = self._connected()
        # Access vigente: se sirve del store sin refresh.
        self.assertEqual(svc.ensure_fresh_token("inst-7"), "twat-1")
        status = svc.status("inst-7")
        self.assertEqual(status["status"], "connected")
        self.assertNotIn("access_token", str(status))

    def test_callback_replay_and_bad_state_rejected(self):
        clock = FakeClock()
        svc, _ = make_service(fake_twitch_ok, clock=clock)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback("state-inexistente", "code")

    def test_wrong_provider_rejected(self):
        from backend.adapters.youtube import YouTubeProvider
        clock = FakeClock()
        svc, _ = make_service(fake_twitch_ok, clock=clock)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        txn = svc._transactions.load(started["transaction_id"])
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
            return fake_twitch_ok(method, url, fields)

        svc, _ = make_service(transport, clock=clock)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        clock.now += 20000  # access de ~4h expirado
        self.assertEqual(svc.ensure_fresh_token("inst-7"), "twat-2")
        svc.disconnect("inst-7")
        self.assertEqual(svc.status("inst-7")["status"], "disconnected")
        self.assertTrue(revoked)


if __name__ == "__main__":
    unittest.main()
