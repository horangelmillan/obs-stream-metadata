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
        # T-073 FB-4 F-080: subcode 33 (objeto web/ID desconocido) no es
        # "input invalido": es rechazo/not-found gestionable desde el dock.
        self.assertEqual(
            classify_facebook_error(
                {"error": {"code": 100, "error_subcode": 33}}, 400).code,
            ErrorCode.PROVIDER_REJECTED)
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


class LifecycleFacebookTest(unittest.TestCase):
    """T-073 FB-4: indicador/on-off/creación-por-dock server-side.

    Sin cambios de routing: todo via `apply_metadata` con `op`
    (update/status/create/end/delete). D6R/D7/D11 ya validados en
    sondas B'/C; aquí contrato offline TDD rojo-primero.
    """

    def _connected(self, transport):
        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        return svc

    def test_status_op_returns_live_state(self):
        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 200, {"id": LIVE_ID, "title": "T",
                             "description": "D", "status": "LIVE"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "status",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"]["status"], "LIVE")
        self.assertEqual(out["result"]["id"], LIVE_ID)

    def test_create_op_posts_me_live_videos(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                seen["payload"] = fields.get("payload", {})
                seen["token"] = fields.get("token")
                return 200, {"id": "999",
                             "secure_stream_url": "rtmps://live-api-s.facebook.com:443/rtmp/abc123"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "Dock live",
                                             "description": "D"})
        self.assertEqual(out["result"]["id"], "999")
        self.assertEqual(out["result"]["secure_stream_url"],
                         "rtmps://live-api-s.facebook.com:443/rtmp/abc123")
        self.assertNotIn("stream_url", out["result"])
        self.assertEqual(seen["payload"].get("title"), "Dock live")
        self.assertNotIn("stream_title", seen["payload"])
        self.assertNotIn("channel_description", seen["payload"])

    def test_create_without_secure_url_omits_field(self):
        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "1000"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "Dock live"})
        self.assertEqual(out["result"]["id"], "1000")
        self.assertNotIn("secure_stream_url", out["result"])
        self.assertNotIn("stream_url", out["result"])

    def test_create_explicit_me_uses_user_token(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                seen["token"] = fields.get("token")
                seen["url"] = url
                return 200, {"id": "1001",
                             "secure_stream_url": "rtmps://x/y"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "Dock live",
                                             "target": "me"})
        self.assertEqual(out["result"]["id"], "1001")
        self.assertEqual(seen["token"], "fbat-1")
        self.assertIn("/me/live_videos", seen["url"])

    def test_ingestion_not_persisted_in_list_or_status(self):
        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "1002",
                             "secure_stream_url": "rtmps://x/secret-key"}
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [{"id": "1002", "title": "Dock live",
                                       "status": "UNPUBLISHED"}]}
            if method == "GET" and "1002" in url:
                return 200, {"id": "1002", "title": "Dock live",
                             "description": "D", "status": "UNPUBLISHED"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        created = svc.apply_metadata("inst-fb", {"op": "create",
                                                 "title": "Dock live"})
        self.assertIn("secure_stream_url", created["result"])
        listed = svc.list_resources("inst-fb")
        for item in listed["resources"]:
            self.assertNotIn("secure_stream_url", item)
            self.assertNotIn("stream_url", item)
        status = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": "1002"})
        self.assertNotIn("secure_stream_url", status["result"])
        self.assertNotIn("stream_url", status["result"])
        # Segunda llamada no recuerda la ingestion.
        listed2 = svc.list_resources("inst-fb")
        for item in listed2["resources"]:
            self.assertNotIn("secure_stream_url", item)

    def test_create_result_has_no_token_or_secret_leak(self):
        import json as _json

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                payload = fields.get("payload", {})
                dumped = _json.dumps({"payload": payload})
                # El payload enviado no loguea/mezcla secretos: solo
                # titulo/descripcion/privacy (el token viaja en el
                # header Authorization, no en el payload).
                for needle in ("fbat", "page-token", "secret"):
                    self.assertNotIn(needle, dumped.lower())
                return 200, {"id": "1003",
                             "secure_stream_url": "rtmps://x/k"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "Dock live"})
        dumped = _json.dumps(out["result"])
        for needle in ("fbat", "page-token", "secret"):
            if needle == "secret":
                # `secure_stream_url` contiene "secure" pero no "secret".
                continue
            self.assertNotIn(needle, dumped.lower())
        self.assertNotIn("secret", dumped.lower().replace("secure", ""))

    def test_go_live_op_posts_live_now(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and url.rstrip("/").endswith("/" + LIVE_ID):
                seen["payload"] = fields.get("payload", {})
                return 200, {"success": True}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "go_live",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"], {"id": LIVE_ID,
                                         "status": "LIVE_NOW"})
        self.assertEqual(seen["payload"], {"status": "LIVE_NOW"})
        self.assertNotIn("secure_stream_url", out["result"])
        self.assertNotIn("stream_url", out["result"])

    def test_end_op_posts_end_live_video(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "end_live_video" in url:
                seen["url"] = url
                return 200, {"success": True}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "end",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"]["id"], LIVE_ID)
        self.assertIn("end_live_video", seen.get("url", ""))

    def test_delete_op_deletes_object(self):
        def transport(method, url, fields):
            if method == "DELETE" and LIVE_ID in url:
                return 200, {"success": True}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "delete",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"]["id"], LIVE_ID)

    def test_unknown_op_is_invalid(self):
        svc = self._connected(fake_facebook_ok)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-fb", {"op": "launch",
                                           "live_video_id": LIVE_ID,
                                           "title": "T"})
        self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)


class UnpublishedCreateStatusTest(unittest.TestCase):
    """Perfil personal: create UNPUBLISHED+EVERYONE y verificación status.

    Sin red, sin secretos reales, solo transports fakes.
    """

    def _connected(self, transport):
        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        return svc

    def test_a_create_sends_unpublished_and_privacy_everyone(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                seen["payload"] = dict(fields.get("payload", {}))
                return 200, {"id": "555"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "T1",
                                             "description": "D1"})
        self.assertEqual(out["result"]["id"], "555")
        self.assertEqual(seen["payload"].get("status"), "UNPUBLISHED")
        self.assertEqual(seen["payload"].get("privacy"),
                         {"value": "EVERYONE"})
        self.assertEqual(seen["payload"].get("title"), "T1")
        self.assertEqual(seen["payload"].get("description"), "D1")

    def test_b_create_then_status_uses_same_id(self):
        seen_urls = []

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "777"}
            if method == "GET" and "777" in url:
                seen_urls.append(url)
                return 200, {"id": "777", "title": "T1",
                             "description": "D1", "status": "UNPUBLISHED",
                             "privacy": {"value": "EVERYONE"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        created = svc.apply_metadata("inst-fb", {"op": "create",
                                                 "title": "T1",
                                                 "description": "D1"})
        new_id = created["result"]["id"]
        self.assertEqual(new_id, "777")
        status = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": new_id})
        self.assertEqual(status["result"]["id"], "777")
        self.assertTrue(any("777" in u for u in seen_urls))
        # La consulta pide privacy para verificar audiencia.
        self.assertTrue(any("privacy" in u for u in seen_urls))

    def test_c_status_compares_all_fields_dict_normalized(self):
        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 200, {"id": LIVE_ID, "title": "T1",
                             "description": "D1", "status": "UNPUBLISHED",
                             "privacy": {"value": "EVERYONE"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "status",
                                             "live_video_id": LIVE_ID})
        res = out["result"]
        self.assertEqual(res, {"id": LIVE_ID, "title": "T1",
                               "description": "D1",
                               "status": "UNPUBLISHED",
                               "privacy": "EVERYONE",
                               "privacy_fallback": False})

    def test_c_status_privacy_string_passthrough(self):
        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 200, {"id": LIVE_ID, "title": "T",
                             "description": "D", "status": "UNPUBLISHED",
                             "privacy": "EVERYONE"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "status",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"]["privacy"], "EVERYONE")

    def test_d_privacy_missing_returns_empty_not_success(self):
        for missing in ({}, {"privacy": None}, {"privacy": {}},
                        {"privacy": {"other": 1}}, {"privacy": 123}):
            def transport(method, url, fields, _m=missing):
                if method == "GET" and LIVE_ID in url:
                    base = {"id": LIVE_ID, "title": "T",
                            "description": "D", "status": "UNPUBLISHED"}
                    base.update(_m)
                    return 200, base
                return fake_facebook_ok(method, url, fields)

            svc = self._connected(transport)
            out = svc.apply_metadata("inst-fb", {"op": "status",
                                                 "live_video_id": LIVE_ID})
            # Campo "" -> el dock lo marcara "audiencia no verificada".
            self.assertEqual(out["result"]["privacy"], "")

    def test_e_status_missing_or_unknown_returned_as_is(self):
        def transport_missing(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 200, {"id": LIVE_ID, "title": "T",
                             "description": "D"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport_missing)
        out = svc.apply_metadata("inst-fb", {"op": "status",
                                             "live_video_id": LIVE_ID})
        self.assertEqual(out["result"]["status"], "")

        def transport_weird(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 200, {"id": LIVE_ID, "title": "T",
                             "description": "D", "status": "WEIRD_X",
                             "privacy": {"value": "EVERYONE"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport_weird)
        out = svc.apply_metadata("inst-fb", {"op": "status",
                                             "live_video_id": LIVE_ID})
        # Aqui no se inventa UNPUBLISHED; el dock bloquea.
        self.assertEqual(out["result"]["status"], "WEIRD_X")

    def test_f_read_errors_mapped_via_classify(self):
        cases = [
            (200, {"error": {"code": 100, "message": "x"}},
             ErrorCode.PROVIDER_REJECTED),
            (404, {"error": {"code": 1}}, ErrorCode.PROVIDER_REJECTED),
            (400, {"error": {"code": 100, "error_subcode": 33}},
             ErrorCode.PROVIDER_REJECTED),
            (400, {"error": {"code": 190}}, ErrorCode.SESSION_EXPIRED),
        ]
        for http_status, payload, expected in cases:
            def transport(method, url, fields, _s=http_status,
                          _p=payload):
                if method == "GET" and LIVE_ID in url:
                    return _s, _p
                return fake_facebook_ok(method, url, fields)

            svc = self._connected(transport)
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("inst-fb", {"op": "status",
                                               "live_video_id": LIVE_ID})
            self.assertEqual(ctx.exception.code, expected,
                             f"{http_status} {payload}")

    def test_g_secure_stream_url_never_persisted_nor_logged(self):
        import json as _json

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "2001",
                             "secure_stream_url": "rtmps://x/abc"}
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [{"id": "2001", "title": "T",
                                       "status": "UNPUBLISHED"}]}
            if method == "GET" and "2001" in url:
                return 200, {"id": "2001", "title": "T",
                             "description": "D", "status": "UNPUBLISHED",
                             "privacy": {"value": "EVERYONE"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        created = svc.apply_metadata("inst-fb", {"op": "create",
                                                 "title": "T"})
        self.assertIn("secure_stream_url", created["result"])
        listed = svc.list_resources("inst-fb")
        dumped_list = _json.dumps(listed)
        self.assertNotIn("secure_stream_url", dumped_list)
        self.assertNotIn("stream_url", dumped_list.replace(
            "secure_stream_url", ""))
        self.assertNotIn("fbat", dumped_list)
        status = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": "2001"})
        dumped_status = _json.dumps(status)
        self.assertNotIn("secure_stream_url", dumped_status)
        self.assertNotIn("fbat", dumped_status)
        for item in listed["resources"]:
            self.assertNotIn("secure_stream_url", item)
            self.assertNotIn("stream_url", item)
        self.assertNotIn("secure_stream_url", status["result"])
        # Errores no exponen tokens ni ingestion.
        def transport_err(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                return 404, {"error": {"code": 1}}
            return fake_facebook_ok(method, url, fields)

        svc2 = self._connected(transport_err)
        try:
            svc2.apply_metadata("inst-fb", {"op": "status",
                                            "live_video_id": LIVE_ID})
            self.fail("debio elevar")
        except AppError as exc:
            blob = _json.dumps({"code": str(exc.code),
                                "detail": str(exc.detail)})
            self.assertNotIn("fbat", blob)
            self.assertNotIn("rtmps", blob)


class PrivacyFallbackStatusTest(unittest.TestCase):
    """Fallback minimo ante fallo por `privacy` (UNA relectura, mismo ID).

    Sin red, sin secretos: fakes inyectados via transport.
    """

    def _connected(self, transport):
        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        return svc

    def test_success_dict_and_string_carry_no_fallback(self):
        for privacy_body in ({"value": "EVERYONE"}, "EVERYONE"):
            def transport(method, url, fields, _p=privacy_body):
                if method == "GET" and LIVE_ID in url:
                    return 200, {"id": LIVE_ID, "title": "T",
                                 "description": "D", "status": "LIVE",
                                 "privacy": _p}
                return fake_facebook_ok(method, url, fields)

            svc = self._connected(transport)
            out = svc.apply_metadata("inst-fb", {"op": "status",
                                                 "live_video_id": LIVE_ID})
            res = out["result"]
            self.assertEqual(res["id"], LIVE_ID)
            self.assertEqual(res["privacy"], "EVERYONE")
            self.assertEqual(res["privacy_fallback"], False)

    def test_privacy_error_then_fallback_success(self):
        cases = [
            (400, {"error": {"code": 100, "error_subcode": 33}}),
            (400, {"error": {"code": 100, "message": "privacy"}}),
            (400, {"error": {"code": 1, "message": "x"}}),
        ]
        for http_status, payload in cases:
            seen_urls = []

            def transport(method, url, fields, _s=http_status,
                          _p=payload):
                if method == "GET" and LIVE_ID in url:
                    seen_urls.append(url)
                    if "privacy" in url:
                        return _s, _p
                    return 200, {"id": LIVE_ID, "title": "Tf",
                                 "description": "Df",
                                 "status": "UNPUBLISHED"}
                return fake_facebook_ok(method, url, fields)

            svc = self._connected(transport)
            out = svc.apply_metadata("inst-fb", {"op": "status",
                                                 "live_video_id": LIVE_ID})
            res = out["result"]
            self.assertEqual(res["id"], LIVE_ID)
            self.assertEqual(res["title"], "Tf")
            self.assertEqual(res["description"], "Df")
            self.assertEqual(res["status"], "UNPUBLISHED")
            self.assertEqual(res["privacy"], "")
            self.assertEqual(res["privacy_fallback"], True)
            # Exactamente 2 lecturas del MISMO ID: con y sin privacy.
            self.assertEqual(len(seen_urls), 2,
                             f"{http_status} {payload}")
            for u in seen_urls:
                self.assertIn(LIVE_ID, u)
            self.assertIn("privacy", seen_urls[0])
            q = dict(urllib.parse.parse_qsl(
                urllib.parse.urlparse(seen_urls[1]).query))
            self.assertEqual(q.get("fields"),
                             "id,title,description,status")
            self.assertNotIn("privacy", q.get("fields", ""))

    def test_both_reads_fail_raises_general_no_partial(self):
        seen_urls = []

        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                seen_urls.append(url)
                return 404, {"error": {"code": 1}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-fb", {"op": "status",
                                           "live_video_id": LIVE_ID})
        self.assertEqual(ctx.exception.code,
                         ErrorCode.PROVIDER_REJECTED)
        self.assertEqual(len(seen_urls), 2)
        for u in seen_urls:
            self.assertIn(LIVE_ID, u)

    def test_no_reuse_of_previous_privacy(self):
        calls = {"n": 0}

        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                calls["n"] += 1
                if calls["n"] == 1:
                    return 200, {"id": LIVE_ID, "title": "T1",
                                 "description": "D1",
                                 "status": "UNPUBLISHED",
                                 "privacy": {"value": "EVERYONE"}}
                # Segunda lectura: intento con privacy falla...
                if "privacy" in url:
                    return 400, {"error": {"code": 100,
                                           "message": "privacy"}}
                # ...y el fallback no trae privacy.
                return 200, {"id": LIVE_ID, "title": "T2",
                             "description": "D2", "status": "LIVE"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        first = svc.apply_metadata("inst-fb", {"op": "status",
                                               "live_video_id": LIVE_ID})
        self.assertEqual(first["result"]["privacy"], "EVERYONE")
        self.assertEqual(first["result"]["privacy_fallback"], False)
        second = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": LIVE_ID})
        # No se reutiliza EVERYONE previa: "" + flag.
        self.assertEqual(second["result"]["title"], "T2")
        self.assertEqual(second["result"]["privacy"], "")
        self.assertEqual(second["result"]["privacy_fallback"], True)

    def test_fallback_result_has_no_ingestion_nor_secrets(self):
        import json as _json

        def transport(method, url, fields):
            if method == "GET" and LIVE_ID in url:
                if "privacy" in url:
                    return 400, {"error": {"code": 100}}
                return 200, {"id": LIVE_ID, "title": "T",
                             "description": "D", "status": "LIVE"}
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "3001",
                             "secure_stream_url": "rtmps://x/k"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        created = svc.apply_metadata("inst-fb", {"op": "create",
                                                 "title": "T"})
        self.assertIn("secure_stream_url", created["result"])
        status = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": LIVE_ID})
        self.assertNotIn("secure_stream_url", status["result"])
        self.assertNotIn("stream_url", _json.dumps(status["result"]))
        dumped = _json.dumps(status["result"])
        self.assertNotIn("fbat", dumped)
        self.assertNotIn("secret", dumped.lower().replace("secure", ""))
        listed = svc.list_resources("inst-fb")
        for item in listed["resources"]:
            self.assertNotIn("secure_stream_url", item)
            self.assertNotIn("stream_url", item)


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


class PersistentCreateAdoptTest(unittest.TestCase):
    """Ruta persistente separada + adopcion solo-lectura (sin red)."""

    def _connected(self, transport):
        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        return svc

    def test_create_persistent_true_sends_field_and_echo(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                seen["payload"] = dict(fields.get("payload", {}))
                return 200, {"id": "9001"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "T",
                                             "persistent": True})
        self.assertEqual(seen["payload"].get(
            "persistent_stream_key_status"), "ENABLE")
        self.assertTrue(out["result"]["persistent_requested"] is True)

    def test_create_default_no_persistent_field_and_false(self):
        seen = {}

        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                seen["payload"] = dict(fields.get("payload", {}))
                return 200, {"id": "9002"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "create",
                                             "title": "T"})
        self.assertNotIn("persistent_stream_key_status", seen["payload"])
        self.assertTrue(out["result"]["persistent_requested"] is False)

    def test_create_persistent_coercion(self):
        for raw, expected in [(1, True), ("1", True), ("true", True),
                              ("True", True), (True, True),
                              (False, False), (0, False), ("0", False),
                              ("yes", False), ("", False), (None, False)]:
            seen = {}

            def transport(method, url, fields, _s=seen):
                if method == "POST" and "live_videos" in url:
                    _s["payload"] = dict(fields.get("payload", {}))
                    return 200, {"id": "9003"}
                return fake_facebook_ok(method, url, fields)

            svc = self._connected(transport)
            data = {"op": "create", "title": "T"}
            if raw is not None:
                data["persistent"] = raw
            out = svc.apply_metadata("inst-fb", data)
            has_field = ("persistent_stream_key_status" in seen["payload"])
            self.assertEqual(has_field, expected, f"raw={raw!r}")
            self.assertEqual(out["result"]["persistent_requested"],
                             expected, f"raw={raw!r}")

    def test_persistent_rejection_nested_and_flat(self):
        msg = "No permission to use persistent stream key param"
        for payload in ({"error": {"code": 1, "message": msg}},
                        {"code": 1, "message": msg}):
            err = classify_facebook_error(payload, 400)
            self.assertEqual(err.code, ErrorCode.PROVIDER_REJECTED)
            self.assertEqual(err.detail,
                             "facebook:persistent-key-not-allowed")

    def test_persistent_rejection_does_not_mask_session_expired(self):
        msg = "persistent stream key issue but auth expired"
        err = classify_facebook_error(
            {"error": {"code": 190, "message": msg}}, 400)
        self.assertEqual(err.code, ErrorCode.SESSION_EXPIRED)

    def test_marker_only_in_create_never_status_list(self):
        def transport(method, url, fields):
            if method == "POST" and "live_videos" in url:
                return 200, {"id": "9004",
                             "secure_stream_url": "rtmps://x/k"}
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [{"id": "9004", "title": "T",
                                       "status": "LIVE"}]}
            if method == "GET" and "9004" in url:
                return 200, {"id": "9004", "title": "T",
                             "description": "D", "status": "LIVE"}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        created = svc.apply_metadata("inst-fb", {"op": "create",
                                                 "title": "T",
                                                 "persistent": True})
        self.assertIn("secure_stream_url", created["result"])
        self.assertTrue(created["result"]["persistent_requested"] is True)
        self.assertNotIn("stream_url", created["result"])
        listed = svc.list_resources("inst-fb")
        for item in listed["resources"]:
            self.assertNotIn("secure_stream_url", item)
            self.assertNotIn("persistent_requested", item)
            self.assertNotIn("persistent_stream_key_status", item)
        status = svc.apply_metadata("inst-fb", {"op": "status",
                                                "live_video_id": "9004"})
        self.assertNotIn("secure_stream_url", status["result"])
        self.assertNotIn("persistent_requested", status["result"])

    def test_adopt_single_live_outcome(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "A1", "title": "Show", "status": "LIVE"},
                    {"id": "A2", "title": "Old", "status": "VOD"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(out["result"],
                         {"adopted": True, "id": "A1",
                          "title": "Show", "status": "LIVE"})

    def test_adopt_live_now_counts_outcome(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "B1", "title": "Now", "status": " live_now "}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertTrue(out["result"]["adopted"] is True)
        self.assertEqual(out["result"]["id"], "B1")

    def test_adopt_none_is_no_active_outcome(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "C1", "title": "Old", "status": "VOD"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(out["result"],
                         {"adopted": False,
                          "reason": "no-active-live", "count": 0})

    def test_adopt_ambiguous_outcome(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "D1", "title": "A", "status": "LIVE"},
                    {"id": "D2", "title": "B", "status": "LIVE_NOW"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(out["result"],
                         {"adopted": False,
                          "reason": "ambiguous-active-live", "count": 2,
                          "candidates": [{"id": "D1", "title": "A"},
                                         {"id": "D2", "title": "B"}]})

    def test_adopt_list_error_goes_through_classify(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 401, {"error": {"code": 190,
                                       "message": "expired"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_adopt_result_has_no_tokens(self):
        import json as _json

        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "E1", "title": "S", "status": "LIVE"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        dumped = _json.dumps(out)
        self.assertNotIn("fbat-1", dumped)
        self.assertNotIn("secret", dumped.lower())
        self.assertNotIn("secure_stream_url", dumped)


class AdoptOutcomeTest(unittest.TestCase):
    """Contrato outcome de adopt (sin elevar por 0/varios).

    1 solo LIVE/LIVE_NOW -> adopted True; 0 -> no-active-live;
    >1 -> ambiguous-active-live con candidates [{id,title}]; fallo de
    transporte/proveedor -> classify (401/403/429/502 distinguibles).
    Sin red Meta: solo fakes inyectados. Sin ingestion en adopt.
    """

    def _connected(self, transport):
        svc, _ = make_service(transport)
        started = svc.start("inst-fb", "http://localhost:0/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-fb")
        return svc

    def test_single_live_adopted(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "A1", "title": "Show", "status": "LIVE"},
                    {"id": "A2", "title": "Old", "status": "VOD"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(out["result"],
                         {"adopted": True, "id": "A1",
                          "title": "Show", "status": "LIVE"})

    def test_none_returns_no_active_live(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "C1", "title": "Old", "status": "VOD"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        res = out["result"]
        self.assertEqual(res,
                         {"adopted": False, "reason": "no-active-live",
                          "count": 0})
        self.assertFalse(res["adopted"])
        self.assertEqual(res["reason"], "no-active-live")
        self.assertEqual(res["count"], 0)

    def test_ambiguous_returns_candidates(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "D1", "title": "A", "status": "LIVE"},
                    {"id": "D2", "title": "B", "status": "LIVE_NOW"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        res = out["result"]
        self.assertEqual(res["adopted"], False)
        self.assertEqual(res["reason"], "ambiguous-active-live")
        self.assertEqual(res["count"], 2)
        self.assertEqual(res["candidates"],
                         [{"id": "D1", "title": "A"},
                          {"id": "D2", "title": "B"}])

    def test_list_error_propagates_classify_session_expired(self):
        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 401, {"error": {"code": 190,
                                       "message": "expired"}}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-fb", {"op": "adopt"})
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_candidates_carry_no_secrets(self):
        import json as _json

        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "D1", "title": "A", "status": "LIVE",
                     "secure_stream_url": "rtmps://x/k",
                     "stream_url": "rtmp://x/k",
                     "access_token": "fbat-1"},
                    {"id": "D2", "title": "B", "status": "LIVE",
                     "secure_stream_url": "rtmps://x/k2"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        res = out["result"]
        self.assertEqual(res["count"], 2)
        for cand in res["candidates"]:
            self.assertEqual(set(cand.keys()), {"id", "title"})
        dumped = _json.dumps(out)
        self.assertNotIn("secure_stream_url", dumped)
        self.assertNotIn("rtmps", dumped)
        self.assertNotIn("fbat-1", dumped)
        self.assertNotIn("access_token", dumped)

    def test_adopt_never_exposes_ingestion(self):
        import json as _json

        def transport(method, url, fields):
            if method == "GET" and "live_videos" in url:
                return 200, {"data": [
                    {"id": "E1", "title": "S", "status": "LIVE",
                     "secure_stream_url": "rtmps://x/k",
                     "stream_url": "rtmp://x/k"}]}
            return fake_facebook_ok(method, url, fields)

        svc = self._connected(transport)
        out = svc.apply_metadata("inst-fb", {"op": "adopt"})
        dumped = _json.dumps(out["result"])
        self.assertNotIn("secure_stream_url", dumped)
        self.assertNotIn("stream_url", dumped.replace(
            "secure_stream_url", ""))
        self.assertNotIn("rtmps", dumped)
        self.assertNotIn("fbat", dumped)

    def test_unknown_op_stays_invalid_request(self):
        svc = self._connected(fake_facebook_ok)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-fb", {"op": "launch",
                                           "live_video_id": LIVE_ID,
                                           "title": "T"})
        self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)


if __name__ == "__main__":
    unittest.main()
