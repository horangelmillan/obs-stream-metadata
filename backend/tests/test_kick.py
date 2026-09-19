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
    if method == "PATCH" and "channels" in url:
        assert fields.get("payload", {}).get("stream_title") == "Nuevo directo"
        return 204, {}
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


class MetadataKickTest(unittest.TestCase):
    """Apply Managed Kick (T-068): PATCH stream_title server-side."""

    def _connected(self):
        svc, _ = make_service(fake_kick_ok)
        started = svc.start("inst-9", "http://localhost:3000/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "code-9")
        self.assertEqual(result["status"], "connected")
        return svc

    def test_apply_title_204(self):
        svc = self._connected()
        out = svc.apply_metadata("inst-9", {"title": "Nuevo directo",
                                            "description": "ignorada"})
        self.assertEqual(out["status"], "updated")
        self.assertEqual(out["provider"], "kick")
        self.assertEqual(out["result"]["title"], "Nuevo directo")

    def test_response_carries_no_tokens(self):
        import json as _json
        svc = self._connected()
        out = svc.apply_metadata("inst-9", {"title": "Nuevo directo"})
        dumped = _json.dumps(out)
        self.assertNotIn("kat-1", dumped)
        self.assertNotIn("krt-1", dumped)
        self.assertNotIn("secret", dumped.lower())

    def test_validation(self):
        svc = self._connected()
        for bad in ({"title": ""}, {"title": "   "}, {}):
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-9", bad)
            self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)

    def test_error_mapping(self):
        cases = [({"message": "bad"}, 400, ErrorCode.INVALID_REQUEST),
                 ({"message": "Unauthorized"}, 401, ErrorCode.SESSION_EXPIRED),
                 ({"message": "Forbidden"}, 403, ErrorCode.AUTHORIZATION)]

        for payload, status, code in cases:
            def transport(method, url, fields, _p=payload, _s=status):
                if method == "PATCH":
                    return _s, _p
                return fake_kick_ok(method, url, fields)

            svc, _ = make_service(transport)
            started = svc.start("inst-9", "http://localhost:3000/cb")
            txn = svc._transactions.load(started["transaction_id"])
            svc.callback(txn["state"], "code-9")
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-9", {"title": "Nuevo directo"})
            self.assertEqual(ctx.exception.code, code)

    def test_headers_on_real_path(self):
        """Bearer viaja en header y método es PATCH (spy sobre urlopen)."""
        import urllib.request
        import backend.adapters.kick as kick_mod
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
            captured["content-type"] = headers.get("content-type")
            captured["method"] = req.get_method()
            captured["url"] = req.full_url
            return FakeResponse()

        urllib.request.urlopen = spy
        try:
            provider = KickProvider(FakeSecrets(), "http://x/cb")
            out = provider.apply_metadata("at-real", {"title": "Nuevo directo"})
        finally:
            urllib.request.urlopen = real_urlopen
        self.assertEqual(captured.get("authorization"), "Bearer at-real")
        self.assertEqual(captured.get("method"), "PATCH")
        self.assertIn("api.kick.com/public/v1/channels", captured.get("url"))
        self.assertEqual(out["title"], "Nuevo directo")

    def test_not_connected_is_authentication(self):
        svc, _ = make_service(fake_kick_ok)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("nadie", {"title": "Nuevo directo"})
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)


class HttpMetadataKickTest(unittest.TestCase):
    """Despacho HTTP genérico /metadata/kick (sin cambios de routing)."""

    @classmethod
    def setUpClass(cls):
        import threading
        from backend.app import create_app
        from backend.config import Settings
        from backend.http_server import serve
        settings = Settings(host="127.0.0.1", port=0,
                            public_base_url="http://127.0.0.1:0")
        provider = KickProvider(FakeSecrets(), "http://localhost:3000/cb",
                                transport=fake_kick_ok)
        svc = ConnectService(provider, InMemoryOAuthTransactionStore(),
                             InMemoryConnectionStore(), InMemoryTokenStore())
        cls.app = create_app(settings=settings, secrets=FakeSecrets(),
                             providers={"kick": svc})
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
        s, _, body = _post(self.base, "/connect/kick", {}, headers=h)
        self.assertEqual(s, 200)
        query = _up.urlparse(_json.loads(body)["authorization_url"]).query
        state = dict(_up.parse_qsl(query))["state"]
        self.app.providers["kick"].callback(state, "code-h")
        return h

    def test_apply_roundtrip(self):
        import json as _json
        from backend.tests.test_http import _post
        h = self._connected_token()
        s, _, body = _post(self.base, "/metadata/kick",
                           {"title": "Nuevo directo",
                            "description": "ignorada"}, headers=h)
        self.assertEqual(s, 200)
        payload = _json.loads(body)
        self.assertEqual(payload["status"], "updated")
        self.assertEqual(payload["provider"], "kick")
        self.assertNotIn("kat-1", body.decode())


if __name__ == "__main__":
    unittest.main()
