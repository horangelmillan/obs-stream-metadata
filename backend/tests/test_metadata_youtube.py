"""Tests FASE 2.1-C: Apply Managed YouTube (tokens server-side, sin red)."""
import json
import threading
import unittest
import urllib.parse

from backend.adapters.youtube import (YouTubeProvider, classify_broadcast_error)
from backend.errors import AppError, ErrorCode
from backend.kernel import Provider
from backend.oauth import ConnectService
from backend.stores import (InMemoryConnectionStore, InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets
from backend.tests.test_http import _get, _post


def make_service(transport, clock=None):
    import time as _time
    clock = clock or _time.time
    provider = YouTubeProvider(FakeSecrets(), "https://x/cb", transport=transport)
    return (ConnectService(provider, InMemoryOAuthTransactionStore(clock=clock),
                           InMemoryConnectionStore(), InMemoryTokenStore(),
                           clock=clock), provider)


def fake_google_meta(method, url, fields):
    """Fake YouTube OAuth + Data API (formas reales, valores sintéticos)."""
    if url.endswith("/token"):
        return 200, {"access_token": "yat-1", "refresh_token": "yrt-1",
                     "expires_in": 3600,
                     "scope": "https://www.googleapis.com/auth/youtube.force-ssl"}
    if "channels?part=snippet" in url:
        return 200, {"items": [{"id": "UC9z", "snippet": {"title": "Canal 9z"}}]}
    if "liveBroadcasts" in url and "broadcastStatus" in url:
        if "broadcastStatus=upcoming" in url:
            return 200, {"items": []}
        return 200, {"items": [
            {"id": "B-ACT", "snippet": {"title": "En vivo", "categoryId": "20"}}]}
    if "liveBroadcasts" in url and "id=" in url and method == "GET":
        return 200, {"items": [
            {"id": "B-ACT", "snippet": {"title": "En vivo", "description": "vieja",
                                        "categoryId": "20"}}]}
    if "liveBroadcasts" in url and method == "PUT":
        body = fields.get("payload", {})
        assert body.get("id") == "B-ACT"
        assert body.get("snippet", {}).get("categoryId") == "20", \
            "el merge debe preservar categoryId (F-021)"
        return 200, {"id": "B-ACT", "snippet": body["snippet"]}
    if "revoke" in url:
        return 200, {}
    raise AssertionError(url)


class FakeClock:
    def __init__(self, now: float = 3_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


class AdapterMetadataTest(unittest.TestCase):
    def test_error_mapping(self):
        err = {"error": {"code": 400, "message": "x",
                         "errors": [{"reason": "invalidTitle"}]}}
        self.assertEqual(classify_broadcast_error(err, 400).code,
                         ErrorCode.INVALID_REQUEST)
        err = {"error": {"code": 404, "message": "x",
                         "errors": [{"reason": "liveBroadcastNotFound"}]}}
        self.assertEqual(classify_broadcast_error(err, 404).code,
                         ErrorCode.PROVIDER_REJECTED)
        err = {"error": {"code": 401, "message": "Invalid Credentials"}}
        self.assertEqual(classify_broadcast_error(err, 401).code,
                         ErrorCode.SESSION_EXPIRED)
        err = {"error": {"code": 403, "message": "x",
                         "errors": [{"reason": "insufficientPermissions"}]}}
        self.assertEqual(classify_broadcast_error(err, 403).code,
                         ErrorCode.AUTHORIZATION)
        self.assertEqual(classify_broadcast_error({"error": {}}, 429).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        self.assertEqual(classify_broadcast_error({"error": {}}, 500).code,
                         ErrorCode.PROVIDER_UNAVAILABLE)

    def test_error_mapping_spaced_messages(self):
        """FASE 2.1-C.1: los mensajes con espacios mapean igual que los
        reasons camelCase (antes caían al default PROVIDER_REJECTED)."""
        err = {"error": {"code": 403, "message":
                         "The request cannot be completed because you have "
                         "exceeded your quota."}}
        out = classify_broadcast_error(err, 403)
        self.assertEqual(out.code, ErrorCode.PROVIDER_RATE_LIMITED)
        err = {"error": {"code": 403, "message": "Rate limit exceeded."}}
        self.assertEqual(classify_broadcast_error(err, 403).code,
                         ErrorCode.PROVIDER_RATE_LIMITED)
        # Reason desconocido: se conserva en el detail (solo logs).
        err = {"error": {"code": 403, "message": "x",
                         "errors": [{"reason": "channelClosed"}]}}
        out = classify_broadcast_error(err, 403)
        self.assertEqual(out.code, ErrorCode.PROVIDER_REJECTED)
        self.assertIn("channelClosed", out.detail)

    def test_validation(self):
        svc, _ = make_service(fake_google_meta)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("inst-x", {"broadcast_id": "B", "title": "",
                                          "description": ""})
        # Sin conexión: AUTHENTICATION antes que la validación de campos.
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)

    def test_other_providers_unsupported(self):
        from backend.adapters.kick import KickProvider
        from backend.adapters.twitch import TwitchProvider
        for provider in (KickProvider(FakeSecrets(), "http://localhost:0/cb"),
                         TwitchProvider(FakeSecrets(), "http://127.0.0.1:0/cb")):
            import time as _time
            svc = ConnectService(
                provider, InMemoryOAuthTransactionStore(),
                InMemoryConnectionStore(), InMemoryTokenStore())
            svc._connections.save("i1", provider.provider.value, {
                "account": {"provider_user_id": "u", "display_name": "N",
                            "scopes": []}, "obtained_at": _time.time()})
            from backend.kernel import Account
            from backend.ports import TokenPair
            svc._tokens.save(Account(provider.provider, "u", "N"),
                             TokenPair("at", "rt", 3600, "s"))
            with self.assertRaises(AppError) as ctx:
                svc.apply_metadata("i1", {"broadcast_id": "B", "title": "T",
                                          "description": ""})
            self.assertEqual(ctx.exception.code, ErrorCode.INVALID_REQUEST)


class FlowMetadataTest(unittest.TestCase):
    def _connected(self, clock=None):
        clock = clock or FakeClock()
        svc, _ = make_service(fake_google_meta, clock=clock)
        started = svc.start("inst-m", "https://x/cb")
        txn = svc._transactions.load(started["transaction_id"])
        result = svc.callback(txn["state"], "code-m")
        self.assertEqual(result["status"], "connected")
        return svc

    def test_list_resources(self):
        svc = self._connected()
        out = svc.list_resources("inst-m")
        self.assertEqual(out["provider"], "youtube")
        self.assertEqual(out["resources"],
                         [{"id": "B-ACT", "title": "En vivo", "status": "active"}])

    def test_apply_title_and_description(self):
        svc = self._connected()
        out = svc.apply_metadata("inst-m", {"broadcast_id": "B-ACT",
                                            "title": "Nuevo",
                                            "description": "Desc nueva"})
        self.assertEqual(out["status"], "updated")
        self.assertEqual(out["result"],
                         {"id": "B-ACT", "title": "Nuevo",
                          "description": "Desc nueva"})

    def test_response_carries_no_tokens(self):
        svc = self._connected()
        out = svc.apply_metadata("inst-m", {"broadcast_id": "B-ACT",
                                            "title": "T", "description": "D"})
        self.assertNotIn("yat-1", json.dumps(out))
        self.assertNotIn("yrt-1", json.dumps(out))
        self.assertNotIn("secret", json.dumps(out).lower())

    def test_token_used_is_server_side(self):
        seen = {}

        def transport(method, url, fields):
            if "liveBroadcasts" in url:
                seen["token"] = fields.get("token")
            return fake_google_meta(method, url, fields)

        svc, _ = make_service(transport)
        started = svc.start("inst-m", "https://x/cb")
        txn = svc._transactions.load(started["transaction_id"])
        svc.callback(txn["state"], "code-m")
        svc.apply_metadata("inst-m", {"broadcast_id": "B-ACT", "title": "T",
                                      "description": ""})
        self.assertEqual(seen.get("token"), "yat-1")

    def test_not_connected_is_authentication(self):
        svc, _ = make_service(fake_google_meta)
        with self.assertRaises(AppError) as ctx:
            svc.apply_metadata("nadie", {"broadcast_id": "B", "title": "T",
                                         "description": ""})
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)
        with self.assertRaises(AppError) as ctx:
            svc.list_resources("nadie")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)


class HttpMetadataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from backend.app import create_app
        from backend.config import Settings
        from backend.http_server import serve
        settings = Settings(host="127.0.0.1", port=0,
                            public_base_url="http://127.0.0.1:0")
        provider = YouTubeProvider(FakeSecrets(), "https://x/cb",
                                   transport=fake_google_meta)
        svc = ConnectService(provider, InMemoryOAuthTransactionStore(),
                             InMemoryConnectionStore(), InMemoryTokenStore())
        cls.app = create_app(settings=settings, secrets=FakeSecrets(),
                             providers={"youtube": svc})
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

    def _session(self):
        import time as _time
        import secrets as _secrets
        from backend.auth import sign_installation_secret
        _, _, body = _post(self.base, "/auth/bootstrap", {})
        creds = json.loads(body)
        ts = int(_time.time())
        nonce = _secrets.token_hex(16)  # nonce único (reúso = 401)
        sig = sign_installation_secret(creds["installation_secret"],
                                       creds["installation_id"], ts, nonce)
        _, _, body = _post(self.base, "/auth/session",
                           {"installation_id": creds["installation_id"],
                            "timestamp": ts, "nonce": nonce,
                            "signature": sig})
        return json.loads(body)["session_token"], creds["installation_id"]

    def _connected_token(self):
        tok, _ = self._session()
        h = {"Authorization": "Bearer " + tok}
        s, _, body = _post(self.base, "/connect/youtube", {}, headers=h)
        self.assertEqual(s, 200)
        # El state viaja en la authorization URL (superficie pública).
        query = urllib.parse.urlparse(json.loads(body)["authorization_url"]).query
        state = dict(urllib.parse.parse_qsl(query))["state"]
        self.app.providers["youtube"].callback(state, "code-h")
        return h

    def test_unauthorized_without_bearer(self):
        s, _, _ = _post(self.base, "/metadata/youtube",
                        {"broadcast_id": "B", "title": "T", "description": ""})
        self.assertEqual(s, 401)
        s, _, _ = _get(self.base, "/metadata/youtube/broadcasts")
        self.assertEqual(s, 401)

    def test_unknown_provider(self):
        h = self._connected_token()
        s, _, _ = _post(self.base, "/metadata/nope",
                        {"broadcast_id": "B", "title": "T"}, headers=h)
        self.assertEqual(s, 400)

    def test_apply_and_list_roundtrip(self):
        h = self._connected_token()
        s, _, body = _get(self.base, "/metadata/youtube/broadcasts", headers=h)
        self.assertEqual(s, 200)
        resources = json.loads(body)["resources"]
        self.assertEqual(resources[0]["id"], "B-ACT")
        s, _, body = _post(self.base, "/metadata/youtube",
                           {"broadcast_id": "B-ACT", "title": "Nuevo T",
                            "description": "Nueva D"}, headers=h)
        self.assertEqual(s, 200)
        payload = json.loads(body)
        self.assertEqual(payload["status"], "updated")
        self.assertEqual(payload["result"]["title"], "Nuevo T")
        self.assertNotIn("yat-1", body.decode())

    def test_validation(self):
        h = self._connected_token()
        s, _, _ = _post(self.base, "/metadata/youtube",
                        {"broadcast_id": "B-ACT", "title": "",
                         "description": ""}, headers=h)
        self.assertEqual(s, 400)
        s, _, _ = _post(self.base, "/metadata/youtube",
                        {"broadcast_id": "B-ACT", "title": "T",
                         "description": 42}, headers=h)
        self.assertEqual(s, 400)


if __name__ == "__main__":
    unittest.main()
