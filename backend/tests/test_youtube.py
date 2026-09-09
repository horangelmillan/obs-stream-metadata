"""Tests T-045: adapter YouTube + flujo Connect con fake provider (sin red)."""
import threading
import unittest
import urllib.parse

from backend.adapters.youtube import YouTubeProvider, classify_token_error
from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Provider
from backend.oauth import ConnectService
from backend.ports import TokenPair
from backend.stores import (InMemoryConnectionStore, InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets


def make_service(transport, clock=None):
    import time as _time
    clock = clock or _time.time
    provider = YouTubeProvider(FakeSecrets(), "http://127.0.0.1:9004/cb",
                               transport=transport)
    return (ConnectService(provider, InMemoryOAuthTransactionStore(clock=clock),
                           InMemoryConnectionStore(), InMemoryTokenStore(),
                           clock=clock), provider)


def fake_google_ok(method, url, fields):
    if url.endswith("/token") and fields.get("grant_type") == "authorization_code":
        assert "client_secret" in fields, "el secret debe viajar al token endpoint"
        return 200, {"access_token": "at-1", "refresh_token": "rt-1",
                     "expires_in": 3600,
                     "scope": "https://www.googleapis.com/auth/youtube.force-ssl"}
    if url.endswith("/token"):
        return 200, {"access_token": "at-2", "expires_in": 3600, "scope": "s"}
    if "revoke" in url:
        return 200, {}
    if "channels" in url:
        return 200, {"items": [{"id": "UC123",
                                "snippet": {"title": "Canal Prueba"}}]}
    raise AssertionError(url)


class FakeClock:
    def __init__(self, now: float = 2_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


class AdapterTest(unittest.TestCase):
    def test_error_mapping(self):
        self.assertEqual(classify_token_error({"error": "invalid_grant"}, 400).code,
                         ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(classify_token_error({"error": "invalid_client"}, 401).code,
                         ErrorCode.INTERNAL)
        self.assertEqual(classify_token_error({"error": "access_denied"}, 400).code,
                         ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_token_error({"error": "x"}, 429).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_token_error({"error": "x"}, 500).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)

    def test_exchange_missing_token_is_rejected(self):
        def transport(method, url, fields):
            return 400, {"error": "invalid_grant"}
        provider = YouTubeProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.PROVIDER_REJECTED)

    def test_refresh_keeps_old_token_when_absent(self):
        def transport(method, url, fields):
            return 200, {"access_token": "at-new", "expires_in": 3600}
        provider = YouTubeProvider(FakeSecrets(), "http://x/cb", transport=transport)
        out = provider.refresh(Account(Provider.YOUTUBE, "u", "N"), "rt-old")
        self.assertEqual((out.access_token, out.refresh_token), ("at-new", "rt-old"))

    def test_channel_identity_empty_is_rejected(self):
        def transport(method, url, fields):
            return 200, {"items": []}
        provider = YouTubeProvider(FakeSecrets(), "http://x/cb", transport=transport)
        with self.assertRaises(AppError):
            provider.channel_identity("at")


class FlowTest(unittest.TestCase):
    def _connected(self, clock=None):
        clock = clock or FakeClock()
        svc, _ = make_service(fake_google_ok, clock=clock)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "auth-code-1")
        return svc, result

    def test_full_connect_maps_account(self):
        svc, result = self._connected()
        self.assertEqual(result["status"], "connected")
        self.assertEqual(result["account"], {"id": "UC123",
                                             "displayName": "Canal Prueba"})
        status = svc.status("inst-1")
        self.assertEqual(status["account"]["displayName"], "Canal Prueba")
        conn = svc.connection("inst-1")
        self.assertEqual(conn.account.provider, Provider.YOUTUBE)

    def test_authorization_url_shape(self):
        svc, _ = make_service(fake_google_ok)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        url = started["authorization_url"]
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(url).query))
        self.assertIn("youtube.force-ssl", q["scope"])
        self.assertEqual(
            (q["response_type"], q["code_challenge_method"], q["access_type"]),
            ("code", "S256", "offline"))
        self.assertNotIn("client_secret", url)

    def test_callback_replay_rejected(self):
        clock = FakeClock()
        svc, _ = make_service(fake_google_ok, clock=clock)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code-1")  # replay

    def test_bad_state_and_expired_rejected(self):
        clock = FakeClock()
        svc, _ = make_service(fake_google_ok, clock=clock)
        with self.assertRaises(AppError):
            svc.callback("state-inexistente", "code")
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        clock.now += 601
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code")

    def test_user_denied_is_authorization(self):
        clock = FakeClock()
        svc, _ = make_service(fake_google_ok, clock=clock)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        with self.assertRaises(AppError) as ctx:
            svc.callback(txn["state"], "", error="access_denied")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHORIZATION)

    def test_wrong_installation_binding(self):
        svc, _ = self._connected()
        self.assertEqual(svc.status("inst-2")["status"], "disconnected")

    def test_refresh_single_flight(self):
        clock = FakeClock()
        calls = []

        def transport(method, url, fields):
            if "channels" in url:
                return fake_google_ok(method, url, fields)
            if fields.get("grant_type") == "refresh_token":
                calls.append(1)
                return 200, {"access_token": "at-r", "expires_in": 3600,
                             "scope": "s"}
            return fake_google_ok(method, url, fields)

        svc, _ = make_service(transport, clock=clock)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        clock.now += 3600  # access expirado
        results = []

        def worker():
            results.append(svc.ensure_fresh_token("inst-1"))

        threads = [threading.Thread(target=worker) for _ in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertTrue(all(r == "at-r" for r in results))
        self.assertEqual(len(calls), 1)  # un solo refresh real

    def test_refresh_rejected_means_reconnect(self):
        def transport(method, url, fields):
            if "channels" in url or fields.get("grant_type") != "refresh_token":
                return fake_google_ok(method, url, fields)
            return 400, {"error": "invalid_grant"}

        clock = FakeClock()
        svc, _ = make_service(transport, clock=clock)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        clock.now += 3600
        with self.assertRaises(AppError) as ctx:
            svc.ensure_fresh_token("inst-1")
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_disconnect_clears_and_revokes(self):
        revoked = []
        base = fake_google_ok

        def transport(method, url, fields):
            if "revoke" in url:
                revoked.append(fields.get("token"))
                return 200, {}
            return base(method, url, fields)

        svc, _ = make_service(transport)
        started = svc.start("inst-1", "http://127.0.0.1:9004/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        svc.disconnect("inst-1")
        self.assertEqual(svc.status("inst-1")["status"], "disconnected")
        self.assertTrue(revoked)  # revoke remoto best-effort ejecutado


if __name__ == "__main__":
    unittest.main()
