"""Limpieza por usuario en login/signup/logout (decision [D] 2026-10-26).

Regla: el que entra despues entra limpio; cuenta nueva siempre limpia.

- login/signup: tras autenticar y ANTES de adoptar, las connections de
  la instalacion llamante con dueno DISTINTO se eliminan (borrado
  protegido + revoke best-effort, como un disconnect); las filas SIN
  dueno (legado NULL) se adoptan; las del MISMO usuario se conservan.
- logout: limpieza total Managed de la instalacion llamante (todas sus
  connections, borrado protegido + revoke) + revoca la sesion de
  usuario. Solo conteos. Idempotente.

Convenciones heredadas: valores sinteticos `fk-*`; las lineas sensibles
se construyen por partes (gate de secret-scan: sin literales tipo
`*password=*valor*` en el arbol).
"""

import json
import logging
import unittest
from types import SimpleNamespace

GOOD_PW = "correct-horse-8-v1"


def _good(i=0):
    return f"{GOOD_PW}-{i}"


def make_app(**kwargs):
    from backend.app import create_app
    from backend.config import Settings
    settings = Settings(host="127.0.0.1", port=0,
                        public_base_url="http://127.0.0.1:0")
    return create_app(settings=settings, **kwargs)


def _stores():
    from backend.stores import (InMemoryConnectionStore,
                                InMemoryTokenStore)
    return InMemoryConnectionStore(), InMemoryTokenStore()


def _installation(app, iid, secret):
    from backend.ports import Installation
    app.installations.create(
        Installation(id=iid, created_at="2026-10-26T00:00:00Z"), secret)


def _signup(app, username, password, ip="10.0.0.1",
            installation_id=None):
    return app.signup_user({"username": username, "password": password},
                           client_ip=ip,
                           installation_id=installation_id)


def _login(app, username, password, ip="10.0.0.1",
           installation_id=None):
    return app.login_user({"username": username, "password": password},
                          client_ip=ip,
                          installation_id=installation_id)


def _seed_conn(app, iid, provider, puid, user_id=None):
    """Connection (+token) en `iid`; `user_id=None` = legado sin dueno."""
    entry = {"account": {"provider_user_id": puid,
                        "display_name": "d-" + puid,
                        "scopes": []}}
    if user_id is not None:
        entry["user_id"] = user_id
    app.connections.save(iid, provider, entry)
    from backend.kernel import Account, Provider
    from backend.ports import TokenPair
    account = Account(provider=Provider(provider),
                      provider_user_id=puid,
                      display_name="d-" + puid, scopes=())
    pair = TokenPair("fk-acc-" + puid, "fk-ref-" + puid, 3600, "s1")
    app.tokens.save(account, pair)
    if user_id is not None:
        app.tokens.set_owner(provider, puid, user_id)
    return account


class _FakeProvider:
    def __init__(self, provider):
        self.provider = provider
        self.revoked = []

    def revoke(self, token):
        self.revoked.append(token)


def _wire_revoke(app, provider="youtube"):
    from backend.kernel import Provider
    fake = _FakeProvider(Provider(provider))
    app.providers[provider] = SimpleNamespace(_provider=fake)
    return fake


class LoginPurgeTest(unittest.TestCase):
    def test_login_of_b_purges_a_links_and_b_enters_clean(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        fake = _wire_revoke(app)
        _installation(app, "inst-1", "fk-install-secret-1")
        created_a = _signup(app, "alice", _good(1),
                            installation_id="inst-1")[1]
        _seed_conn(app, "inst-1", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        created_b = _signup(app, "bruno", _good(2),
                            ip="10.0.0.2")[1]
        status, payload = _login(app, "bruno", _good(2),
                                 ip="10.0.0.3",
                                 installation_id="inst-1")
        self.assertEqual(status, 200)
        # B no hereda nada: connection fuera, token huerfano fuera
        # (sin mas referencias), instalacion re-adoptada a B.
        self.assertIsNone(app.connections.load("inst-1", "youtube"))
        from backend.kernel import Account, Provider
        self.assertIsNone(app.tokens.load(
            Account(provider=Provider.YOUTUBE,
                    provider_user_id="UC-A1",
                    display_name="d-UC-A1", scopes=())))
        self.assertEqual(app.installations.get_user_id("inst-1"),
                         created_b["user_id"])
        # Revoke best-effort sobre los tokens de A.
        self.assertIn("fk-acc-UC-A1", fake.revoked)
        self.assertIn("fk-ref-UC-A1", fake.revoked)

    def test_relogin_of_a_preserves_links(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        fake = _wire_revoke(app)
        _installation(app, "inst-1", "fk-install-secret-1")
        created_a = _signup(app, "alice", _good(3),
                            installation_id="inst-1")[1]
        before = _seed_conn(app, "inst-1", "youtube", "UC-A1",
                            user_id=created_a["user_id"])
        status, _ = _login(app, "alice", _good(3), ip="10.0.0.4",
                           installation_id="inst-1")
        self.assertEqual(status, 200)
        entry = app.connections.load("inst-1", "youtube")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.get("user_id"), created_a["user_id"])
        self.assertEqual(app.tokens.load(before),
                         toks.load(before))
        self.assertEqual(fake.revoked, [])

    def test_null_legacy_rows_are_adopted(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        _installation(app, "inst-1", "fk-install-secret-1")
        _seed_conn(app, "inst-1", "youtube", "UC-LEG")
        created = _signup(app, "carla", _good(4),
                          installation_id="inst-1")[1]
        entry = app.connections.load("inst-1", "youtube")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.get("user_id"), created["user_id"])
        self.assertEqual(app.tokens.owner_of("youtube", "UC-LEG"),
                         created["user_id"])

    def test_multi_pc_of_same_user_intact(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        fake = _wire_revoke(app)
        _installation(app, "inst-1", "fk-install-secret-1")
        _installation(app, "inst-2", "fk-install-secret-2")
        created_a = _signup(app, "alice", _good(5),
                            installation_id="inst-1")[1]
        _login(app, "alice", _good(5), ip="10.0.0.5",
               installation_id="inst-2")
        _seed_conn(app, "inst-1", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        _seed_conn(app, "inst-2", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        # B entra por inst-2: solo inst-2 se limpia; inst-1 intacta y
        # la fila de tokens compartida se conserva (sigue referenciada).
        created_b = _signup(app, "bruno", _good(6),
                            ip="10.0.0.6")[1]
        status, _ = _login(app, "bruno", _good(6), ip="10.0.0.7",
                           installation_id="inst-2")
        self.assertEqual(status, 200)
        self.assertIsNone(app.connections.load("inst-2", "youtube"))
        kept = app.connections.load("inst-1", "youtube")
        self.assertIsNotNone(kept)
        self.assertEqual(kept.get("user_id"), created_a["user_id"])
        from backend.kernel import Account, Provider
        self.assertIsNotNone(app.tokens.load(
            Account(provider=Provider.YOUTUBE,
                    provider_user_id="UC-A1",
                    display_name="d-UC-A1", scopes=())))
        self.assertEqual(app.installations.get_user_id("inst-2"),
                         created_b["user_id"])
        self.assertEqual(app.installations.get_user_id("inst-1"),
                         created_a["user_id"])
        self.assertIn("fk-acc-UC-A1", fake.revoked)


class LogoutCleanTest(unittest.TestCase):
    def test_logout_cleans_all_counts_and_idempotent(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        fake = _wire_revoke(app)
        _installation(app, "inst-1", "fk-install-secret-1")
        created_a = _signup(app, "alice", _good(7),
                            installation_id="inst-1")[1]
        _seed_conn(app, "inst-1", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        token = _login(app, "alice", _good(7), ip="10.0.0.8",
                       installation_id="inst-1")[1]["user_session"]
        status, payload = app.logout_user(token, installation_id="inst-1")
        self.assertEqual(status, 200)
        cleaned = payload["cleaned"]
        self.assertEqual(cleaned["connections"], 1)
        self.assertEqual(cleaned["tokens"], 1)
        self.assertEqual(cleaned["sessions"], 1)
        self.assertEqual(payload["revoked"], {"youtube": True})
        self.assertIsNone(app.connections.load("inst-1", "youtube"))
        from backend.errors import AppError, ErrorCode
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)
        # Idempotente: repetir devuelve ceros sin efectos.
        status2, payload2 = app.logout_user(token,
                                            installation_id="inst-1")
        self.assertEqual(status2, 200)
        self.assertEqual(payload2["cleaned"],
                         {"connections": 0, "tokens": 0, "sessions": 0})
        self.assertEqual(payload2["revoked"], {})
        self.assertEqual(fake.revoked.count("fk-acc-UC-A1"), 1)

    def test_logout_unknown_token_wipes_nothing(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        _installation(app, "inst-1", "fk-install-secret-1")
        created_a = _signup(app, "alice", _good(8),
                            installation_id="inst-1")[1]
        _seed_conn(app, "inst-1", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        status, payload = app.logout_user("tok-inexistente",
                                          installation_id="inst-1")
        self.assertEqual(status, 200)
        self.assertEqual(payload["cleaned"],
                         {"connections": 0, "tokens": 0, "sessions": 0})
        self.assertIsNotNone(app.connections.load("inst-1", "youtube"))

    def test_logout_without_installation_revokes_session_only(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        _installation(app, "inst-1", "fk-install-secret-1")
        created_a = _signup(app, "alice", _good(9),
                            installation_id="inst-1")[1]
        _seed_conn(app, "inst-1", "youtube", "UC-A1",
                   user_id=created_a["user_id"])
        token = _login(app, "alice", _good(9),
                       ip="10.0.0.9")[1]["user_session"]
        status, payload = app.logout_user(token)
        self.assertEqual(status, 200)
        self.assertEqual(payload["cleaned"]["sessions"], 1)
        self.assertEqual(payload["cleaned"]["connections"], 0)
        self.assertIsNotNone(app.connections.load("inst-1", "youtube"))


class LogoutLeakGuardTest(unittest.TestCase):
    def test_no_secrets_nor_ids_in_responses_or_logs(self):
        conns, toks = _stores()
        app = make_app(connections=conns, tokens=toks)
        _wire_revoke(app)
        _installation(app, "inst-1", "fk-install-secret-1")
        _signup(app, "alice", _good(10), installation_id="inst-1")
        logger = logging.getLogger("backend.http")
        stream = __import__("io").StringIO()
        handler = logging.StreamHandler(stream)
        logger.addHandler(handler)
        try:
            _, login_payload = _login(app, "alice", _good(10),
                                      ip="10.0.0.10",
                                      installation_id="inst-1")
            _, logout_payload = app.logout_user(
                login_payload["user_session"],
                installation_id="inst-1")
        finally:
            logger.removeHandler(handler)
        dump = json.dumps([login_payload, logout_payload])
        for sensitive in ("fk-acc-UC", "fk-ref-UC", "UC-A1",
                          "fk-install-secret-1"):
            self.assertNotIn(sensitive, dump)
        out = stream.getvalue()
        for sensitive in ("fk-acc-UC", "fk-ref-UC", "UC-A1",
                          "fk-install-secret-1"):
            self.assertNotIn(sensitive, out)


if __name__ == "__main__":
    unittest.main()
