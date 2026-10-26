"""Tests Fase 3 (spec 2026-10-26 §4): auth doble en Managed (TDD rojo→verde).

Decisiones fijadas aquí con evidencia (pendientes [P] del spec §11):
- Doble sesión en TODAS las rutas Managed de conexión y metadata
  (POST /connect/<provider>, GET .../status, GET /metadata/.../broadcasts,
  POST /metadata/<provider>): `Authorization: Bearer <install T-044>`
  vigente + `X-User-Session: <user Fase 2>` vigente.
- Falta cualquiera de las dos (ausente/inválida/expirada/revocada, de
  cualquier lado) → 401 normalizado idéntico: mismo código
  `authentication_error` + mismo mensaje, mismo `detail` interno
  (`MANAGED_AUTH_DETAIL`, reutilizado en ambos casos).
- Excepciones: `GET /connect/<provider>/callback` sigue sin bearer pero
  vincula connection+tokens al `user_id` adoptado de la instalación que
  inició la transacción; públicas /health|/ready|/version intactas;
  /auth/* y /users/* con su control propio vigente.
- Propagación: connections.user_id + tokens.owner_user_id = adoptado;
  instalación solo opera lo de su propio user_id (multi-PC mismo usuario
  SÍ; otro usuario NUNCA).
- Auditoría de rechazos: solo IDs opacos + códigos/conteos; jamás
  tokens/hashes/passwords en logs.
- Independent no existe en backend (BYO-app directo): ningún cambio a
  sus flujos; aquí se verifica que el despacho público/auth/users sigue
  intacto.

Convenciones heredadas: valores sintéticos `fk-*`; sin literales tipo
`*password=*valor` en el árbol (gate de secret-scan).
"""
import json
import logging
import secrets
import threading
import unittest
import urllib.parse

from backend.oauth import ConnectService
from backend.stores import (InMemoryConnectionStore,
                             InMemoryOAuthTransactionStore,
                             InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets
from backend.tests.test_http import _get, _post
from backend.tests.test_metadata_youtube import fake_google_meta

GOOD_PW = "correct-horse-7-dual-1"


def make_app():
    from backend.app import create_app
    from backend.config import Settings
    from backend.adapters.youtube import YouTubeProvider
    settings = Settings(host="127.0.0.1", port=0,
                        public_base_url="http://127.0.0.1:0")
    provider = YouTubeProvider(FakeSecrets(), "https://x/cb",
                               transport=fake_google_meta)
    transactions = InMemoryOAuthTransactionStore()
    connections = InMemoryConnectionStore()
    tokens = InMemoryTokenStore()
    svc = ConnectService(provider, transactions, connections, tokens)
    return create_app(settings=settings, secrets=FakeSecrets(),
                      providers={"youtube": svc},
                      transactions=transactions,
                      connections=connections, tokens=tokens)


def _install_session(base):
    """Bootstrap + session de instalación (T-044). Devuelve (iid, bearer)."""
    import time as _time
    from backend.auth import sign_installation_secret
    _, _, body = _post(base, "/auth/bootstrap", {})
    creds = json.loads(body)
    ts = int(_time.time())
    nonce = secrets.token_hex(16)
    sig = sign_installation_secret(creds["installation_secret"],
                                   creds["installation_id"], ts, nonce)
    _, _, body = _post(base, "/auth/session",
                       {"installation_id": creds["installation_id"],
                        "timestamp": ts, "nonce": nonce,
                        "signature": sig})
    return (creds["installation_id"],
            json.loads(body)["session_token"])


def _user_session(base, install_bearer, username=None, password=None):
    """Signup + login adoptando la instalación llamante. Devuelve dict
    con user_id, user_session y headers duales listos."""
    username = username or ("dual-" + secrets.token_hex(4))
    password = password or GOOD_PW
    auth = {"Authorization": "Bearer " + install_bearer}
    status, _, body = _post(base, "/users/signup",
                            {"username": username, "password": password},
                            headers=auth)
    # Multi-PC: el usuario ya existe (misma cuenta, otra instalación).
    assert status in (201, 409), body
    created = json.loads(body) if status == 201 else None
    status, _, body = _post(base, "/users/login",
                            {"username": username, "password": password},
                            headers=auth)
    assert status == 200, body
    logged = json.loads(body)
    headers = {"Authorization": "Bearer " + install_bearer,
               "X-User-Session": logged["user_session"]}
    user_id = created["user_id"] if created is not None else None
    if user_id is None:
        # 409 en signup: el user_id sale de /users/me con la sesión nueva.
        status, _, me_body = _get(base, "/users/me",
                                  headers={"X-User-Session":
                                           logged["user_session"]})
        assert status == 200, me_body
        user_id = json.loads(me_body)["user_id"]
    return {"user_id": user_id, "username": username,
            "headers": headers, "install_bearer": install_bearer,
            "user_token": logged["user_session"]}


def _error_of(body):
    payload = json.loads(body)
    err = dict(payload["error"])
    err.pop("requestId", None)
    return err


class DualAuthHttpTest(unittest.TestCase):
    def setUp(self):
        # App fresca por test: los rate-limiters son por instancia y el
        # bootstrap anónimo está limitado por IP (misma 127.0.0.1).
        from backend.http_server import serve
        self.app = make_app()
        self.server = serve(self.app)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def _setup(self):
        iid, bearer = _install_session(self.base)
        dual = _user_session(self.base, bearer)
        dual["installation_id"] = iid
        return dual

    def test_connect_without_user_session_is_401(self):
        _, bearer = _install_session(self.base)
        status, _, body = _post(
            self.base, "/connect/youtube", {},
            {"Authorization": "Bearer " + bearer})
        self.assertEqual(status, 401)
        self.assertEqual(_error_of(body)["code"], "authentication_error")

    def test_dual_flow_ok(self):
        dual = self._setup()
        h = dual["headers"]
        status, _, body = _post(self.base, "/connect/youtube", {},
                                headers=h)
        self.assertEqual(status, 200, body)
        query = urllib.parse.urlparse(
            json.loads(body)["authorization_url"]).query
        state = dict(urllib.parse.parse_qsl(query))["state"]
        status, _, body = _get(
            self.base,
            "/connect/youtube/callback?state=" + state + "&code=code-h")
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)["status"], "connected")
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=h)
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)["status"], "connected")
        status, _, body = _get(self.base,
                               "/metadata/youtube/broadcasts", headers=h)
        self.assertEqual(status, 200, body)
        status, _, body = _post(self.base, "/metadata/youtube",
                                {"broadcast_id": "B-ACT", "title": "Nuevo T",
                                 "description": "Nueva D"}, headers=h)
        self.assertEqual(status, 200, body)

    def test_401_identical_without_either_side(self):
        """Sin una de las dos sesiones → 401 idéntico (mismo código y
        mensaje), en status y en metadata, falte el lado que falte."""
        dual = self._setup()
        bearer = dual["install_bearer"]
        utoken = dual["user_token"]
        only_install = {"Authorization": "Bearer " + bearer}
        only_user = {"X-User-Session": utoken}
        bodies = []
        status, _, body = _get(self.base, "/connect/youtube/status")
        self.assertEqual(status, 401)
        bodies.append(body)
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=only_install)
        self.assertEqual(status, 401)
        bodies.append(body)
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=only_user)
        self.assertEqual(status, 401)
        bodies.append(body)
        meta = {"broadcast_id": "B-ACT", "title": "T", "description": ""}
        status, _, body = _post(self.base, "/metadata/youtube", meta,
                                headers=only_install)
        self.assertEqual(status, 401)
        bodies.append(body)
        status, _, body = _post(self.base, "/metadata/youtube", meta,
                                headers=only_user)
        self.assertEqual(status, 401)
        bodies.append(body)
        first = _error_of(bodies[0])
        self.assertEqual(first["code"], "authentication_error")
        for other in bodies[1:]:
            self.assertEqual(_error_of(other), first)

    def test_normalized_detail_reused_both_sides(self):
        """El `detail` interno (solo logs) tampoco distingue lados:
        la misma constante en ambos casos."""
        from backend.errors import AppError, ErrorCode
        from backend.http_server import MANAGED_AUTH_DETAIL
        app = self.app
        iid, bearer = _install_session(self.base)
        dual = _user_session(self.base, bearer)
        install_only = {"Authorization": "Bearer " + bearer}
        user_only = {"X-User-Session": dual["user_token"]}
        with self.assertRaises(AppError) as ctx1:
            app.check_managed_access(install_only, provider="youtube")
        with self.assertRaises(AppError) as ctx2:
            app.check_managed_access(user_only, provider="youtube")
        for ctx in (ctx1, ctx2):
            self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)
            self.assertEqual(ctx.exception.http_status(), 401)
            self.assertEqual(ctx.exception.detail, MANAGED_AUTH_DETAIL)
        self.assertEqual(ctx1.exception.detail, ctx2.exception.detail)
        # Con ambas vigentes no hay error y vuelve (record, user).
        record, user = app.check_managed_access(dual["headers"],
                                                provider="youtube")
        self.assertEqual(record.installation_id, iid)
        self.assertEqual(user.id, dual["user_id"])

    def test_callback_links_owner_to_adopted(self):
        dual = self._setup()
        h = dual["headers"]
        _, _, body = _post(self.base, "/connect/youtube", {}, headers=h)
        query = urllib.parse.urlparse(
            json.loads(body)["authorization_url"]).query
        state = dict(urllib.parse.parse_qsl(query))["state"]
        status, _, _ = _get(
            self.base,
            "/connect/youtube/callback?state=" + state + "&code=code-h")
        self.assertEqual(status, 200)
        entry = self.app.connections.load(dual["installation_id"],
                                          "youtube")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.get("user_id"), dual["user_id"])
        self.assertEqual(
            self.app.tokens.owner_of("youtube", "UC9z"), dual["user_id"])

    def test_isolation_between_users(self):
        """Otro usuario NUNCA opera connections/tokens ajenos: 401
        idéntico al de sesión ausente."""
        owner = self._setup()
        h_owner = owner["headers"]
        _, _, body = _post(self.base, "/connect/youtube", {},
                           headers=h_owner)
        query = urllib.parse.urlparse(
            json.loads(body)["authorization_url"]).query
        state = dict(urllib.parse.parse_qsl(query))["state"]
        _, _, _ = _get(self.base, "/connect/youtube/callback?state="
                       + state + "&code=code-h")
        other = self._setup()
        # La instalación ajena apunta a la misma cuenta de proveedor
        # pero sigue marcada del dueño: el intruso no la opera.
        self.app.connections.save(
            other["installation_id"], "youtube",
            {"account": {"provider_user_id": "UC9z",
                         "display_name": "Canal 9z",
                         "scopes": []},
             "obtained_at": 0.0, "user_id": owner["user_id"]})
        h_other = other["headers"]
        status, _, intruder = _get(self.base, "/connect/youtube/status",
                                   headers=h_other)
        self.assertEqual(status, 401)
        status, _, naked = _get(self.base, "/connect/youtube/status")
        self.assertEqual(status, 401)
        self.assertEqual(_error_of(intruder), _error_of(naked))
        meta = {"broadcast_id": "B-ACT", "title": "T", "description": ""}
        status, _, intruder = _post(self.base, "/metadata/youtube", meta,
                                    headers=h_other)
        self.assertEqual(status, 401)
        self.assertEqual(_error_of(intruder), _error_of(naked))

    def test_multi_install_same_user_ok(self):
        """Otra instalación del MISMO usuario sí opera (multi-PC)."""
        first = self._setup()
        iid2, bearer2 = _install_session(self.base)
        second = _user_session(self.base, bearer2,
                               username=first["username"],
                               password=GOOD_PW)
        h2 = second["headers"]
        status, _, body = _post(self.base, "/connect/youtube", {},
                                headers=h2)
        self.assertEqual(status, 200, body)
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=h2)
        self.assertEqual(status, 200, body)
        # La primera instalación sigue operativa.
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=first["headers"])
        self.assertEqual(status, 200, body)
        self.assertEqual(
            self.app.installations.get_user_id(iid2), first["user_id"])

    def test_public_auth_users_control_intact(self):
        """Independent no existe en backend: públicas intactas, /auth/*
        y /users/* con su control propio, despacho genérico intacto."""
        status, _, body = _get(self.base, "/health")
        self.assertEqual(status, 200)
        status, _, _ = _get(self.base, "/version")
        self.assertEqual(status, 200)
        status, _, body = _post(self.base, "/auth/bootstrap", {})
        self.assertEqual(status, 201)
        status, _, body = _post(
            self.base, "/users/signup",
            {"username": "pub-" + secrets.token_hex(4),
             "password": GOOD_PW})
        self.assertEqual(status, 201, body)
        # Despacho genérico intacto: proveedor desconocido con doble
        # sesión válida → 400 del dispatcher, no 401 ni ruta nueva.
        dual = self._setup()
        status, _, _ = _post(self.base, "/connect/nope", {},
                             headers=dual["headers"])
        self.assertEqual(status, 400)
        status, _, _ = _get(self.base, "/connect/nope/status",
                            headers=dual["headers"])
        self.assertEqual(status, 400)

    def test_reject_audit_leak_guard(self):
        """Los rechazos dobles auditan sin fugas: ni tokens, ni hashes,
        ni contraseñas en logs."""
        from backend.logging_setup import RedactingFilter
        stream = __import__("io").StringIO()
        handler = logging.StreamHandler(stream)
        logger = self.app.log
        logger.addHandler(handler)
        logger.addFilter(RedactingFilter())
        try:
            _, bearer = _install_session(self.base)
            dual = _user_session(self.base, bearer)
            only_install = {"Authorization": "Bearer " + bearer}
            _get(self.base, "/connect/youtube/status")
            _get(self.base, "/connect/youtube/status",
                 headers=only_install)
            _post(self.base, "/metadata/youtube",
                  {"broadcast_id": "B", "title": "T", "description": ""},
                  headers=only_install)
        finally:
            logger.removeHandler(handler)
        output = stream.getvalue()
        self.assertIn("managed", output)
        self.assertNotIn(bearer, output)
        self.assertNotIn(dual["user_token"], output)
        self.assertNotIn(GOOD_PW, output)
        self.assertNotIn("hash", output)


if __name__ == "__main__":
    unittest.main()
