"""Tests Fase 5a (spec 2026-10-26 §5; TDD rojo→verde): `POST /users/erase`.

Borrado en cascada por USUARIO (cuenta Managed), operación distinta de
`POST /privacy/erase` F-C2 (por instalación, T-064): no se fusionan.

Decisiones fijadas aquí con evidencia (pendiente [P] del spec §11):
- Auth doble Managed (Fase 3): instalación vigente (T-044) + usuario
  vigente (Fase 2). Sin user-session → 401 normalizado idéntico al de
  Fase 3 (mismo código y mensaje; mismo `detail` interno).
- Orden F-030: DELETEs locales primero (tokens copiados en memoria),
  revoke remoto best-effort al final; un fallo de red jamás resucita
  estado. Idempotente: usuario desconocido/ya borrado → conteos a cero.
- Fila de tokens compartida: se conserva mientras OTRA connection la
  referencie (mismo usuario, multi-PC: hasta la última referencia); si
  la referencia superviviente es de OTRO usuario jamás se toca
  (aislamiento Fase 3) aunque el `owner` apunte al borrado.
- Respuesta solo conteos por tabla + `revoked` por provider (bools):
  `{"erased": {"users", "installations", "sessions", "connections",
  "tokens", "transactions"}, "revoked": {provider: bool}}`.
  `sessions` agrega sesiones de instalación y de usuario (mismo store
  por defecto; si están separados se suman). Sin secretos ni IDs.
- Contrato para Fase 4 (placeholder `eraseAccountButton_`): el plugin
  activa el botón con `POST /users/erase` con doble sesión
  (`Authorization: Bearer <install>` + `X-User-Session: <user>`);
  200 con el cuerpo de arriba, 401 idéntico a Fase 3 sin user-session.
  El bearer usado queda revocado como efecto (era sesión del usuario).

Convenciones heredadas: valores sintéticos `fk-*`; sin literales tipo
`*password=*valor` en el árbol (gate de secret-scan).
"""
import io
import json
import logging
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request

from backend.tests.test_users_dualauth import (
    GOOD_PW,
    _error_of,
    _install_session,
    _user_session,
    make_app,
)


def _connect(base, headers, provider="youtube"):
    """Connect + callback OAuth (fake) con doble sesión. Devuelve el body."""
    status, _, body = _post(base, "/connect/" + provider, {}, headers)
    assert status == 200, body
    query = urllib.parse.urlparse(
        json.loads(body)["authorization_url"]).query
    state = dict(urllib.parse.parse_qsl(query))["state"]
    status, _, body = _get(
        base, "/connect/" + provider + "/callback?state=" + state
        + "&code=code-erase")
    assert status == 200, body
    return json.loads(body)


def _post(base, path, payload, headers=None):
    req = urllib.request.Request(
        base + path, data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", **(headers or {})},
        method="POST")
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, dict(resp.headers), resp.read().decode()
    except urllib.error.HTTPError as exc:
        return exc.code, dict(exc.headers), exc.read().decode()


def _get(base, path, headers=None):
    req = urllib.request.Request(base + path, headers=headers or {},
                                 method="GET")
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, dict(resp.headers), resp.read().decode()
    except urllib.error.HTTPError as exc:
        return exc.code, dict(exc.headers), exc.read().decode()


class EraseHttpTest(unittest.TestCase):
    def setUp(self):
        from backend.http_server import serve
        self.app = make_app()
        self.server = serve(self.app)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(
            target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def _setup_owner(self):
        iid, bearer = _install_session(self.base)
        dual = _user_session(self.base, bearer)
        dual["installation_id"] = iid
        _connect(self.base, dual["headers"])
        return dual

    def test_erase_removes_all_and_counts(self):
        owner = self._setup_owner()
        other = self._setup_other()
        status, _, body = _post(self.base, "/users/erase", {},
                               headers=owner["headers"])
        self.assertEqual(status, 200, body)
        payload = json.loads(body)
        # 1 install session + 1 user session; 1 txn del `start`.
        # tokens=0: el fake siempre devuelve la misma cuenta (UC9z) y
        # el otro usuario la sigue referenciando (fila compartida).
        self.assertEqual(payload["erased"],
                         {"users": 1, "installations": 1, "sessions": 2,
                          "connections": 1, "tokens": 0,
                          "transactions": 1})
        self.assertEqual(payload["revoked"], {"youtube": True})
        # El usuario ya no existe (ni su sesión, ni su instalación).
        self.assertIsNone(
            self.app.users.load_by_id(owner["user_id"]))
        self.assertIsNone(self.app.installations.load(
            owner["installation_id"]))
        self.assertIsNone(self.app.connections.load(
            owner["installation_id"], "youtube"))
        status, _, _ = _get(self.base, "/users/me",
                            headers={"X-User-Session":
                                     owner["user_token"]})
        self.assertEqual(status, 401)
        # El otro usuario sigue intacto y conectado.
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=other["headers"])
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)["status"], "connected")

    def _setup_other(self):
        iid, bearer = _install_session(self.base)
        dual = _user_session(self.base, bearer)
        dual["installation_id"] = iid
        _connect(self.base, dual["headers"])
        return dual

    def test_second_erase_zeros(self):
        """Idempotente: tras el borrado, repetir da conteos a cero."""
        owner = self._setup_owner()
        status, _, body = _post(self.base, "/users/erase", {},
                               headers=owner["headers"])
        self.assertEqual(status, 200, body)
        repeat = self.app.erase_user(owner["user_id"])
        self.assertEqual(repeat["erased"],
                         {"users": 0, "installations": 0, "sessions": 0,
                          "connections": 0, "tokens": 0,
                          "transactions": 0})
        self.assertEqual(repeat["revoked"], {})

    def test_erase_unknown_user_zeros(self):
        out = self.app.erase_user("user-missing-fk")
        self.assertEqual(out["erased"],
                         {"users": 0, "installations": 0, "sessions": 0,
                          "connections": 0, "tokens": 0,
                          "transactions": 0})
        self.assertEqual(out["revoked"], {})

    def test_shared_same_user_kept_until_last(self):
        """Multi-PC del mismo usuario: la fila compartida cae solo con
        la última referencia (connections=2, tokens=1)."""
        first = self._setup_owner()
        iid2, bearer2 = _install_session(self.base)
        second = _user_session(self.base, bearer2,
                               username=first["username"],
                               password=GOOD_PW)
        _connect(self.base, second["headers"])
        status, _, body = _post(self.base, "/users/erase", {},
                               headers=first["headers"])
        self.assertEqual(status, 200, body)
        payload = json.loads(body)
        self.assertEqual(payload["erased"]["connections"], 2)
        self.assertEqual(payload["erased"]["tokens"], 1)
        self.assertEqual(payload["erased"]["users"], 1)
        self.assertEqual(payload["erased"]["installations"], 2)
        self.assertEqual(payload["revoked"], {"youtube": True})
        from backend.kernel import Account, Provider
        from backend.ports import TokenPair
        probe = Account(Provider.YOUTUBE, "UC9z", "Canal 9z", ())
        self.assertIsNone(self.app.tokens.load(probe))

    def test_other_user_token_never_touched(self):
        """Cuenta de proveedor compartida entre usuarios: el borrado de
        uno jamás toca la fila (el otro sigue conectado)."""
        owner = self._setup_owner()
        other = self._setup_other()
        status, _, body = _post(self.base, "/users/erase", {},
                               headers=owner["headers"])
        self.assertEqual(status, 200, body)
        payload = json.loads(body)
        self.assertEqual(payload["erased"]["connections"], 1)
        self.assertEqual(payload["erased"]["tokens"], 0)
        status, _, body = _get(self.base, "/connect/youtube/status",
                               headers=other["headers"])
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)["status"], "connected")

    def test_no_user_session_401_identical(self):
        """Sin X-User-Session → 401 idéntico al de Fase 3."""
        owner = self._setup_owner()
        only_install = {"Authorization": owner["headers"]["Authorization"]}
        status, _, body = _post(self.base, "/users/erase", {},
                               headers=only_install)
        self.assertEqual(status, 401)
        status, _, naked = _get(self.base, "/connect/youtube/status")
        self.assertEqual(status, 401)
        self.assertEqual(_error_of(body), _error_of(naked))

    def test_leak_guard(self):
        """Respuesta y logs sin secretos ni IDs sensibles."""
        from backend.logging_setup import RedactingFilter
        stream = io.StringIO()
        handler = logging.StreamHandler(stream)
        logger = self.app.log
        logger.addHandler(handler)
        logger.addFilter(RedactingFilter())
        try:
            owner = self._setup_owner()
            status, _, body = _post(self.base, "/users/erase", {},
                                   headers=owner["headers"])
            self.assertEqual(status, 200, body)
        finally:
            logger.removeHandler(handler)
        dump = body
        for secret in (owner["user_token"],
                       owner["headers"]["Authorization"], GOOD_PW,
                       "UC9z", owner["username"], owner["user_id"]):
            self.assertNotIn(secret, dump, secret[:4] + "… filtrado")
        output = stream.getvalue()
        self.assertIn("users erase", output)
        for secret in (owner["user_token"],
                       owner["headers"]["Authorization"], GOOD_PW):
            self.assertNotIn(secret, output)


class StoreEraseTest(unittest.TestCase):
    """Semántica de los métodos nuevos por store (InMemory; Pg/SQLite
    comparten contrato y pasan los mismos gates)."""

    def test_installation_list_for_user(self):
        from backend.ports import Installation
        from backend.stores import InMemoryInstallationStore
        insts = InMemoryInstallationStore()
        insts.create(Installation(id="i-1"), "s1")
        insts.create(Installation(id="i-2"), "s2")
        insts.set_user_id("i-1", "u-1")
        self.assertEqual(insts.list_for_user("u-1"), ["i-1"])
        self.assertEqual(insts.list_for_user("u-9"), [])

    def test_session_delete_for_user(self):
        from backend.stores import InMemorySessionStore
        sess = InMemorySessionStore()
        sess.save_session("s1", {"user_id": "u-1"})
        sess.save_session("s2", {"installation_id": "i-1"})
        sess.save_session("s3", {"user_id": "u-2"})
        self.assertEqual(sess.delete_for_user("u-1"), 1)
        self.assertIsNone(sess.load_session("s1"))
        self.assertIsNotNone(sess.load_session("s2"))

    def test_connection_list_for_user(self):
        from backend.stores import InMemoryConnectionStore
        conns = InMemoryConnectionStore()
        conns.save("i-1", "youtube",
                   {"account": {"provider_user_id": "UC1"},
                    "user_id": "u-1"})
        conns.save("i-9", "youtube",
                   {"account": {"provider_user_id": "UC2"}})
        pairs = conns.list_for_user("u-1", ("i-9",))
        self.assertEqual(pairs, [("i-1", "youtube"), ("i-9", "youtube")])
        # Sin instalaciones propias: nada (la tupla es propiedad del
        # llamante; una instalación ajena jamás se pasa como propia).
        self.assertEqual(conns.list_for_user("u-2", ()), [])

    def test_user_delete(self):
        from backend.stores import InMemoryUserStore
        from backend.users import User, hash_password
        store = InMemoryUserStore()
        user = User(id="u-1", username="ada",
                    password_hash=hash_password("x" * 12))
        store.create(user)
        self.assertTrue(store.delete("u-1"))
        self.assertIsNone(store.load_by_id("u-1"))
        self.assertFalse(store.delete("u-1"))


if __name__ == "__main__":
    unittest.main()
