"""Tests HTTP en vivo: health/ready/version, errores JSON, auth gate, requestId."""
import json
import threading
import unittest
import urllib.request
import urllib.error

from backend.app import create_app
from backend.config import Settings
from backend.http_server import serve


def _get(base, path, headers=None):
    req = urllib.request.Request(base + path, method="GET", headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def _post(base, path, payload, headers=None):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(base + path, data=data, method="POST",
                                 headers={"Content-Type": "application/json",
                                          **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


class HttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        settings = Settings(host="127.0.0.1", port=0)
        cls.app = create_app(settings=settings)
        cls.server = serve(cls.app)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def _served_app(self, **kwargs):
        from backend.tests.test_adapters import FakeSecrets
        settings = Settings(host="127.0.0.1", port=0,
                            public_base_url="http://127.0.0.1:0")
        app = create_app(settings=settings, secrets=FakeSecrets(),
                         enable_youtube=True, **kwargs)
        server = serve(app)
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        return f"http://127.0.0.1:{port}"

    def _session(self, base):
        import time as _time
        from backend.auth import sign_installation_secret
        _, _, body = _post(base, "/auth/bootstrap", {})
        creds = json.loads(body)
        ts = int(_time.time())
        nonce = "n" * 32
        sig = sign_installation_secret(creds["installation_secret"],
                                       creds["installation_id"], ts, nonce)
        _, _, body = _post(base, "/auth/session",
                           {"installation_id": creds["installation_id"],
                            "timestamp": ts, "nonce": nonce,
                            "signature": sig})
        return json.loads(body)["session_token"]

    def test_connect_alias_and_generic_routes(self):
        """El alias /connect/youtube debe comportarse como el genérico
        (regresión: AttributeError app.youtube → 500)."""
        base = self._served_app()
        token = self._session(base)
        for path in ("/connect/youtube",):
            status, _, body = _post(
                base, path, {},
                {"Authorization": "Bearer " + token})
            self.assertEqual(status, 200, path)
            payload = json.loads(body)
            self.assertIn("authorization_url", payload)
            self.assertNotIn("client_secret", json.dumps(payload))

    def test_health(self):
        status, headers, body = _get(self.base, "/health")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["status"], "ok")
        self.assertIn("version", payload)
        self.assertTrue(headers.get("X-Request-Id"))

    def test_version(self):
        status, _, body = _get(self.base, "/version")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["api"], "v1")

    def test_ready_ok_and_not_ready(self):
        status, _, body = _get(self.base, "/ready")
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(body)["ready"])
        app = create_app(settings=Settings(host="127.0.0.1", port=0),
                         ready_check=lambda: (False, "db-down"))
        server = serve(app)
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            status, _, body = _get(f"http://127.0.0.1:{port}", "/ready")
            self.assertEqual(status, 503)
            self.assertFalse(json.loads(body)["ready"])
        finally:
            server.shutdown()
            server.server_close()

    def test_unknown_path_is_safe_401(self):
        """El gate va primero: ruta desconocida no pública → 401 sin fugas."""
        status, _, body = _get(self.base, "/nope")
        self.assertEqual(status, 401)
        payload = json.loads(body)
        self.assertEqual(payload["error"]["code"], "authentication_error")
        self.assertTrue(payload["error"]["requestId"])

    def test_non_public_requires_session(self):
        status, _, body = _get(self.base, "/connect/youtube")
        self.assertEqual(status, 401)
        self.assertEqual(json.loads(body)["error"]["code"], "authentication_error")

    def test_session_allows_reserved_paths_shape(self):
        """El gate existe: con sesión vigente emitida por /auth/* no hay 401."""
        import time as _time
        from backend.auth import sign_installation_secret
        status, _, body = _post(self.base, "/auth/bootstrap", {})
        self.assertEqual(status, 201)
        creds = json.loads(body)
        ts = int(_time.time())
        nonce = "n" * 32
        sig = sign_installation_secret(creds["installation_secret"],
                                       creds["installation_id"], ts, nonce)
        status, _, body = _post(self.base, "/auth/session",
                                {"installation_id": creds["installation_id"],
                                 "timestamp": ts, "nonce": nonce,
                                 "signature": sig})
        self.assertEqual(status, 200)
        token = json.loads(body)["session_token"]
        status, _, _ = _get(self.base, "/connect/youtube",
                            {"Authorization": "Bearer " + token})
        self.assertNotEqual(status, 401)


if __name__ == "__main__":
    unittest.main()
