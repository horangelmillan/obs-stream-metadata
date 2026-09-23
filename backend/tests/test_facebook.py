"""Tests FB-3 (T-072): adapter Facebook Managed + flujo Connect (sin red).

Espejo de test_twitch.py / test_kick.py (plan FB-3 Step 1, TDD rojo-primero).
Decisiones D1-D14 en docs/T0FB-RESEARCH.md; Graph API v25/v26.

D8: sin refresh clasico. El par guardado es TokenPair(access, "", expires)
con expires real (user ~60d); el page token derivado no se persiste jamas
(solo memoria durante el apply). Sin refresh -> SESSION_EXPIRED -> reconnect.
F-074/F-075: el listado es best-effort (puede venir vacio con objetos
legibles por ID); el producto es ID-centrico, sin inventar broadcasts.
D10: revoke DELETE /me/permissions. Jamas publish_to_groups/email.
"""
import unittest
import urllib.parse

from backend.adapters.facebook import (FacebookProvider,
                                       classify_facebook_error)
from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Provider
from backend.oauth import ConnectService
from backend.stores import (InMemoryConnectionStore,
                            InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets

USER_ID = "12345"
PAGE_ID = "page-9"
LIVE_ID = "28266780719650968"


def make_service(transport, clock=None):
    import time as _time
    clock = clock or _time.time
    provider = FacebookProvider(FakeSecrets(), "http://localhost:0/cb",
                                transport=transport)
    return (ConnectService(provider, InMemoryOAuthTransactionStore(clock=clock),
                           InMemoryConnectionStore(), InMemoryTokenStore(),
                           clock=clock), provider)


def fake_facebook_ok(method, url, fields):
    if "oauth/access_token" in url:
        if fields.get("grant_type") == "fb_exchange_token":
            assert "client_secret" in fields, "long-lived solo server-side"
            assert "fb_exchange_token" in fields
            # Sin refresh_token clasico (D8): solo access + expires real.
            return 200, {"access_token": "fbat-1", "token_type": "bearer",
                         "expires_in": 5184000}
        # Code exchange Meta: sin grant_type (implicito). PKCE + secret.
        assert "client_secret" in fields, \
            "el secret viaja al token endpoint (solo backend)"
        assert "code_verifier" in fields, "PKCE S256 tambien en Managed"
        assert "redirect_uri" in fields and "code" in fields
        return 200, {"access_token": "fbat-short", "token_type": "bearer",
                     "expires_in": 7200}
    if method == "DELETE" and url.rstrip("/").endswith("/me/permissions"):
        return 200, True
    if method == "GET" and "accounts" in url:
        assert fields.get("token") == "fbat-1"
        return 200, {"data": [{"id": PAGE_ID, "name": "Page Nueve",
                               "access_token": "page-token-9"}]}
    if method == "GET" and "live_videos" in url:
        # Best-effort (F-074/F-075): vacio aunque existan objetos.
        return 200, {"data": []}
    if method == "GET" and ("/me?" in url or url.rstrip("/").endswith("/me")):
        return 200, {"id": USER_ID, "name": "fb-test"}
    if method == "POST" and LIVE_ID in url:
        payload = fields.get("payload", {})
        assert payload.get("title") == "Nuevo directo"
        assert "channel_description" not in payload, \
            "channel_description es del canal, jamas como descripcion (D2)"
        assert "snippet" not in payload and "stream_title" not in payload
        assert fields.get("token") in ("fbat-1", "page-token-9")
        return 200, {"success": True}
    raise AssertionError(method + " " + url)


class FakeClock:
    def __init__(self, now: float = 3_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


class AdapterTest(unittest.TestCase):
    def test_error_mapping(self):
        self.assertEqual(classify_facebook_error({"code": 190}, 400).code,
                         ErrorCode.SESSION_EXPIRED)
        for code in (1363120, 1363144):
            self.assertEqual(
                classify_facebook_error({"code": code}, 200).code,
                ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_facebook_error({"code": 10}, 403).code,
                         ErrorCode.AUTHORIZATION)
        for code in (613, 4, 17):
            self.assertEqual(
                classify_facebook_error({"code": code}, 200).code,
                ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_facebook_error({"code": 100}, 400).code,
                         ErrorCode.INVALID_REQUEST)
        self.assertEqual(classify_facebook_error({"code": 1}, 401).code,
                         ErrorCode.SESSION_EXPIRED)
        self.assertEqual(classify_facebook_error({"code": 1}, 403).code,
                         ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_facebook_error({"code": 1}, 404).code,
                         ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(classify_facebook_error({"code": 1}, 429).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_facebook_error({"code": 1}, 500).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)

    def test_exchange_missing_token_is_rejected(self):
        def transport(method, url, fields):
            return 400, {"error": {"code": 100, "message": "Invalid"}}
        provider = FacebookProvider(FakeSecrets(), "http://x/cb",
                                    transport=transport)
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.PROVIDER_REJECTED)

    def test_long_lived_failure_keeps_short_token(self):
        def transport(method, url, fields):
            if fields.get("grant_type") == "fb_exchange_token":
                return 400, {"error": {"code": 100}}
            return fake_facebook_ok(method, url, fields)
        provider = FacebookProvider(FakeSecrets(), "http://x/cb",
                                    transport=transport)
        out = provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(out.access_token, "fbat-short")
        self.assertEqual(out.refresh_token, "")

    def test_refresh_without_classic_refresh_is_session_expired(self):
        provider = FacebookProvider(FakeSecrets(), "http://x/cb",
                                    transport=fake_facebook_ok)
        with self.assertRaises(AppError) as ctx:
            provider.refresh(Account(Provider.FACEBOOK, "u", "N"), "")
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_identity_empty_is_rejected(self):
        def transport(method, url, fields):
            return 200, {"id": "", "name": ""}
        provider = FacebookProvider(FakeSecrets(), "http://x/cb",
                                    transport=transport)
        with self.assertRaises(AppError):
            provider.fetch_identity("fbat")

    def test_missing_credentials_is_internal(self):
        provider = FacebookProvider(FakeSecrets(missing=True), "http://x/cb")
        with self.assertRaises(AppError) as ctx:
            provider.exchange("code", "verifier", "http://x/cb")
        self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)

    def test_required_secret_names(self):
        self.assertEqual(FacebookProvider.required_secret_names,
                         ("FB_APP_ID", "FB_APP_SECRET"))

    def test_scopes_minimal_never_groups_nor_email(self):
        scopes = FacebookProvider.SCOPES
        # F-079: perfil-only (Consumer rechaza pages_*: F-072).
        self.assertEqual(scopes, ("publish_video",))
        self.assertNotIn("publish_to_groups", scopes)
        self.assertNotIn("email", scopes)


class FlowTest(unittest.TestCase):
    def _connected(self, clock=None):
        clock = clock or FakeClock()
        svc, _ = make_service(fake_facebook_ok, clock=clock)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "auth-code-fb")
        return svc, result

    def test_full_connect_maps_identity(self):
        svc, result = self._connected()
        self.assertEqual(result["status"], "connected")
        self.assertEqual(result["provider"], "facebook")
        self.assertEqual(result["account"], {"id": USER_ID,
                                             "displayName": "fb-test"})
        conn = svc.connection("inst-fb")
        self.assertEqual(conn.account.provider, Provider.FACEBOOK)

    def test_stored_pair_has_no_refresh_and_real_expiry(self):
        svc, _ = self._connected()
        conn = svc.connection("inst-fb")
        pair = svc._tokens.load(conn.account)
        self.assertEqual(pair.refresh_token, "")
        self.assertGreater(pair.expires_in, 3600)

    def test_authorization_url_shape(self):
        svc, _ = make_service(fake_facebook_ok)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        url = started["authorization_url"]
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(url).query))
        self.assertTrue(url.startswith(
            "https://www.facebook.com/v26.0/dialog/oauth?"))
        self.assertEqual(q["response_type"], "code")
        self.assertIn("publish_video", q["scope"])
        self.assertNotIn("publish_to_groups", q["scope"])
        self.assertNotIn("email", q["scope"].split(","))
        self.assertEqual(q["code_challenge_method"], "S256")
        self.assertTrue(q["state"])
        self.assertNotIn("client_secret", url)

    def test_tokens_stored_securely_and_fresh(self):
        svc, _ = self._connected()
        self.assertEqual(svc.ensure_fresh_token("inst-fb"), "fbat-1")
        status = svc.status("inst-fb")
        self.assertEqual(status["status"], "connected")
        self.assertNotIn("access_token", str(status))

    def test_expired_user_token_is_session_expired(self):
        clock = FakeClock()
        svc, _ = self._connected(clock=clock)
        clock.now += 5184000 + 60  # long-lived (~60d) vencido
        with self.assertRaises(AppError) as ctx:
            svc.ensure_fresh_token("inst-fb")
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_callback_replay_and_bad_state_rejected(self):
        clock = FakeClock()
        svc, _ = make_service(fake_facebook_ok, clock=clock)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback(txn["state"], "code-1")
        with self.assertRaises(AppError):
            svc.callback("state-inexistente", "code")

    def test_wrong_provider_rejected(self):
        from backend.adapters.youtube import YouTubeProvider
        clock = FakeClock()
        svc, _ = make_service(fake_facebook_ok, clock=clock)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        yt = ConnectService(
            YouTubeProvider(FakeSecrets(), "http://127.0.0.1:9004/cb"),
            svc._transactions, svc._connections, svc._tokens, clock=clock)
        with self.assertRaises(AppError):
            yt.callback(txn["state"], "code-1")
        self.assertEqual(svc.status("inst-otra")["status"], "disconnected")

    def test_disconnect_revokes_permissions(self):
        clock = FakeClock()
        revoked = []

        def transport(method, url, fields):
            if method == "DELETE" and "permissions" in url:
                revoked.append(True)
                return 200, True
            return fake_facebook_ok(method, url, fields)

        svc, _ = make_service(transport, clock=clock)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")
        svc.disconnect("inst-fb")
        self.assertEqual(svc.status("inst-fb")["status"], "disconnected")
        self.assertTrue(revoked)


class MetadataFacebookTest(unittest.TestCase):
    """Apply Managed Facebook: POST /{live-video-id} server-side."""

    def _connected(self):
        svc, _ = make_service(fake_facebook_ok)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "code-fb")
        self.assertEqual(result["status"], "connected")
        return svc

    def test_apply_title_desc(self):
        svc = self._connected()
        out = svc.apply_metadata("inst-fb", {"live_video_id": LIVE_ID,
                                             "title": "Nuevo directo",
                                             "description": "Desc E2E"})
        self.assertEqual(out["status"], "updated")
        self.assertEqual(out["provider"], "facebook")
        self.assertEqual(out["result"]["id"], LIVE_ID)
        self.assertEqual(out["result"]["title"], "Nuevo directo")
        self.assertEqual(out["result"]["description"], "Desc E2E")

    def test_apply_to_page_uses_derived_page_token(self):
        seen = []

        def transport(method, url, fields):
            if method == "POST" and LIVE_ID in url:
                seen.append(fields.get("token"))
            return fake_facebook_ok(method, url, fields)

        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        out = svc.apply_metadata("inst-fb", {"live_video_id": LIVE_ID,
                                             "title": "Nuevo directo",
                                             "target": PAGE_ID})
        self.assertEqual(out["status"], "updated")
        self.assertEqual(seen, ["page-token-9"])

    def test_response_carries_no_tokens(self):
        import json as _json
        svc = self._connected()
        out = svc.apply_metadata("inst-fb", {"live_video_id": LIVE_ID,
                                             "title": "Nuevo directo"})
        dumped = _json.dumps(out)
        self.assertNotIn("fbat-1", dumped)
        self.assertNotIn("page-token-9", dumped)
        self.assertNotIn("secret", dumped.lower())

    def test_validation(self):
        svc = self._connected()
        for bad in ({"live_video_id": "", "title": "T"},
                    {"title": "T"},
                    {"live_video_id": LIVE_ID, "title": ""},
                    {"live_video_id": LIVE_ID, "title": "x" * 255}):
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-fb", bad)
            self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)

    def test_error_mapping(self):
        cases = [({"error": {"code": 100}}, 400, ErrorCode.INVALID_REQUEST),
                 ({"error": {"code": 190}}, 400, ErrorCode.SESSION_EXPIRED),
                 ({"error": {"code": 1363120}}, 200,
                  ErrorCode.AUTHORIZATION),
                 ({"error": {"code": 10}}, 403, ErrorCode.AUTHORIZATION)]

        for payload, status, code in cases:
            def transport(method, url, fields, _p=payload, _s=status):
                if method == "POST" and LIVE_ID in url:
                    return _s, _p
                return fake_facebook_ok(method, url, fields)

            svc, _ = make_service(transport)
            started = svc.start("inst-fb", "http://localhost:0/cb")
            txn = svc._transactions.load(started["transaction_id"])
            svc.callback(txn["state"], "code-fb")
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-fb", {"live_video_id": LIVE_ID,
                                               "title": "Nuevo directo"})
            self.assertEqual(ctx.exception.code, code)

    def test_list_resources_best_effort_may_be_empty(self):
        svc = self._connected()
        out = svc.list_resources("inst-fb")
        self.assertEqual(out["provider"], "facebook")
        # F-074/F-075: vacio aunque existan objetos legibles por ID.
        self.assertEqual(out["resources"], [])

    def test_list_resources_maps_items(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [{"id": LIVE_ID, "title": "T",
                                       "status": "LIVE"}]}
            return fake_facebook_ok(method, url, fields)

        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        out = svc.list_resources("inst-fb")
        self.assertEqual(out["resources"],
                         [{"id": LIVE_ID, "title": "T", "status": "LIVE"}])

    def test_headers_on_real_path(self):
        """Bearer viaja en header y metodo es POST (spy sobre urlopen)."""
        import urllib.request
        import backend.adapters.facebook as fb_mod
        captured = {}
        real_urlopen = urllib.request.urlopen

        class FakeResponse:
            status = 200

            def read(self):
                return b'{"success": true}'

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def spy(req, timeout=None):
            headers = {k.lower(): v for k, v in req.header_items()}
            captured["authorization"] = headers.get("authorization")
            captured["method"] = req.get_method()
            captured["url"] = req.full_url
            return FakeResponse()

        urllib.request.urlopen = spy
        try:
            provider = FacebookProvider(FakeSecrets(), "http://x/cb")
            out = provider.apply_metadata("at-real",
                                          {"live_video_id": LIVE_ID,
                                           "title": "Nuevo directo"})
        finally:
            urllib.request.urlopen = real_urlopen
        self.assertEqual(captured.get("authorization"), "Bearer at-real")
        self.assertEqual(captured.get("method"), "POST")
        self.assertIn(LIVE_ID, captured.get("url"))
        self.assertEqual(out["id"], LIVE_ID)

    def test_not_connected_is_authentication(self):
        svc, _ = make_service(fake_facebook_ok)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("nadie", {"live_video_id": LIVE_ID,
                                         "title": "Nuevo directo"})
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)


class HttpMetadataFacebookTest(unittest.TestCase):
    """Despacho HTTP generico /metadata/facebook (sin cambios de routing)."""

    @classmethod
    def setUpClass(cls):
        import threading
        from backend.app import create_app
        from backend.config import Settings
        from backend.http_server import serve
        settings = Settings(host="127.0.0.1", port=0,
                            public_base_url="http://127.0.0.1:0")
        provider = FacebookProvider(FakeSecrets(), "http://localhost:0/cb",
                                    transport=fake_facebook_ok)
        svc = ConnectService(provider, InMemoryOAuthTransactionStore(),
                             InMemoryConnectionStore(), InMemoryTokenStore())
        cls.app = create_app(settings=settings, secrets=FakeSecrets(),
                             providers={"facebook": svc})
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
        s, _, body = _post(self.base, "/connect/facebook", {}, headers=h)
        self.assertEqual(s, 200)
        query = _up.urlparse(_json.loads(body)["authorization_url"]).query
        state = dict(_up.parse_qsl(query))["state"]
        self.app.providers["facebook"].callback(state, "code-h")
        return h

    def test_apply_roundtrip(self):
        import json as _json
        from backend.tests.test_http import _post
        h = self._connected_token()
        s, _, body = _post(self.base, "/metadata/facebook",
                           {"live_video_id": LIVE_ID,
                            "title": "Nuevo directo",
                            "description": "Desc"}, headers=h)
        self.assertEqual(s, 200)
        payload = _json.loads(body)
        self.assertEqual(payload["status"], "updated")
        self.assertEqual(payload["provider"], "facebook")
        self.assertNotIn("fbat-1", body.decode())


if __name__ == "__main__":
    unittest.main()
