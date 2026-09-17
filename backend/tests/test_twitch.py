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


def fake_twitch_meta(method, url, fields):
    if "helix/channels" in url:
        assert fields.get("token") == "twat-1", "token Managed server-side"
        assert fields.get("client_id") == "test-tw-id", "Client-Id server-side"
        assert set(fields["payload"]) == {"title"}, \
            "solo titulo (Twitch no tiene descripcion equivalente)"
        assert "broadcaster_id=18200" in url
        return 204, {}
    return fake_twitch_ok(method, url, fields)


class MetadataTest(unittest.TestCase):
    def _connected(self):
        svc, _ = make_service(fake_twitch_meta)
        started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "code-7")
        self.assertEqual(result["status"], "connected")
        return svc

    def test_apply_title_204(self):
        svc = self._connected()
        out = svc.apply_metadata("inst-7", {"title": "Nuevo directo",
                                            "broadcaster_id": "18200",
                                            "description": "ignorada"})
        self.assertEqual(out["status"], "updated")
        self.assertEqual(out["result"],
                         {"broadcaster_id": "18200", "title": "Nuevo directo"})

    def test_response_carries_no_tokens(self):
        import json as _json
        svc = self._connected()
        out = svc.apply_metadata("inst-7", {"title": "T",
                                            "broadcaster_id": "18200"})
        self.assertNotIn("twat-1", _json.dumps(out))
        self.assertNotIn("twrt-1", _json.dumps(out))
        self.assertNotIn("secret", _json.dumps(out).lower())

    def test_validation(self):
        svc = self._connected()
        for bad in ({"title": "", "broadcaster_id": "18200"},
                    {"title": "x" * 141, "broadcaster_id": "18200"},
                    {"title": "T", "broadcaster_id": ""}):
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-7", bad)
            self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)

    def test_helix_error_mapping(self):
        cases = [({"status": 400, "message": "Title too long"}, 400,
                  ErrorCode.INVALID_REQUEST),
                 ({"status": 401, "message": "Unauthorized"}, 401,
                  ErrorCode.SESSION_EXPIRED),
                 ({"status": 403, "message": "Forbidden"}, 403,
                  ErrorCode.AUTHORIZATION)]

        for payload, status, code in cases:
            def transport(method, url, fields, _p=payload, _s=status):
                if "helix/channels" in url:
                    return _s, _p
                return fake_twitch_ok(method, url, fields)

            svc, _ = make_service(transport)
            started = svc.start("inst-7", "http://127.0.0.1:8080/cb")
            txn = svc._transactions.load(started["transaction_id"])
            svc.callback(txn["state"], "code-7")
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-7", {"title": "T",
                                              "broadcaster_id": "18200"})
            self.assertEqual(ctx.exception.code, code)

    def test_headers_on_real_path(self):
        """Bearer + Client-Id viajan en headers (spy sobre urlopen)."""
        import urllib.request
        import backend.adapters.twitch as tw_mod
        captured = {}
        real_urlopen = urllib.request.urlopen

        class FakeResponse:
            status = 204

            def read(self):
                return b""

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def spy(req, timeout=None):
            headers = {k.lower(): v for k, v in req.header_items()}
            captured["authorization"] = headers.get("authorization")
            captured["client-id"] = headers.get("client-id")
            captured["method"] = req.get_method()
            return FakeResponse()

        urllib.request.urlopen = spy
        try:
            provider = TwitchProvider(FakeSecrets(), "http://x/cb")
            out = provider.apply_metadata("at-real",
                                          {"title": "T",
                                           "broadcaster_id": "18200"})
        finally:
            urllib.request.urlopen = real_urlopen
        self.assertEqual(captured.get("authorization"), "Bearer at-real")
        self.assertEqual(captured.get("client-id"), "test-tw-id")
        self.assertEqual(captured.get("method"), "PATCH")
        self.assertEqual(out, {"broadcaster_id": "18200", "title": "T"})

    def test_not_connected_is_authentication(self):
        svc, _ = make_service(fake_twitch_meta)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("nadie", {"title": "T",
                                         "broadcaster_id": "18200"})
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)


class HttpMetadataTwitchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import threading
        from backend.app import create_app
        from backend.config import Settings
        from backend.http_server import serve
        settings = Settings(host="127.0.0.1", port=0,
                            public_base_url="http://127.0.0.1:0")
        provider = TwitchProvider(FakeSecrets(), "http://x/cb",
                                  transport=fake_twitch_meta)
        svc = ConnectService(provider, InMemoryOAuthTransactionStore(),
                             InMemoryConnectionStore(), InMemoryTokenStore())
        cls.app = create_app(settings=settings, secrets=FakeSecrets(),
                             providers={"twitch": svc})
        cls.server = serve(cls.app)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever,
                                      daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def _connected_token(self):
        import json as _json
        import time as _time
        import secrets as _secrets
        import urllib.parse as _up
        from backend.auth import sign_installation_secret
        from backend.tests.test_http import _post
        _, _, body = _post(self.base, "/auth/bootstrap", {})
        creds = _json.loads(body)
        ts = int(_time.time())
        nonce = _secrets.token_hex(16)
        sig = sign_installation_secret(creds["installation_secret"],
                                       creds["installation_id"], ts, nonce)
        _, _, body = _post(self.base, "/auth/session",
                           {"installation_id": creds["installation_id"],
                            "timestamp": ts, "nonce": nonce,
                            "signature": sig})
        tok = _json.loads(body)["session_token"]
        h = {"Authorization": "Bearer " + tok}
        s, _, body = _post(self.base, "/connect/twitch", {}, headers=h)
        self.assertEqual(s, 200)
        query = _up.urlparse(_json.loads(body)["authorization_url"]).query
        state = dict(_up.parse_qsl(query))["state"]
        self.app.providers["twitch"].callback(state, "code-h")
        return h

    def test_apply_roundtrip(self):
        import json as _json
        from backend.tests.test_http import _post
        h = self._connected_token()
        s, _, body = _post(self.base, "/metadata/twitch",
                           {"title": "Nuevo directo",
                            "broadcaster_id": "18200"}, headers=h)
        self.assertEqual(s, 200)
        payload = _json.loads(body)
        self.assertEqual(payload["status"], "updated")
        self.assertNotIn("twat-1", body.decode())


if __name__ == "__main__":
    unittest.main()
