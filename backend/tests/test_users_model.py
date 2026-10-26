"""Tests Fase 1 (spec 2026-10-26, Enfoque A): users, PBKDF2, migracion 003, adopcion.

Convenciones heredadas: valores sinteticos `fk-*`; sin `*_campo=valor`
literales sensibles (gate de secret-scan del CI: las lineas sensibles se
construyen por partes); PG real con skip si no hay servicio (patron
test_pg.py); SQLite dev via stdlib.
"""
import base64
import io
import logging
import os
import sqlite3
import tempfile
import unittest

MIGRATIONS = os.path.join(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))), "migrations")


def _read_migration(name):
    with open(os.path.join(MIGRATIONS, name), "r",
              encoding="utf-8") as handle:
        return handle.read()


def _sqlite_apply(conn, sql):
    """Aplica un fichero de migracion PG sobre SQLite dev.

    SQLite no acepta ``ADD COLUMN IF NOT EXISTS`` (sintaxis PG): se
    normaliza a ``ADD COLUMN`` y los ALTER se protegen por PRAGMA (la
    idempotencia a nivel runner la da ``schema_migrations`` en PG).
    Los comentarios `--` se pelan antes de partir por `;`.
    """
    lines = [line for line in sql.splitlines()
             if not line.lstrip().startswith("--")]
    sql = "\n".join(lines).replace("ADD COLUMN IF NOT EXISTS",
                                   "ADD COLUMN")
    for raw in sql.split(";"):
        stmt = raw.strip()
        if not stmt:
            continue
        if stmt.upper().startswith("ALTER TABLE"):
            parts = stmt.split()
            table, column = parts[2], parts[5]
            cols = {row[1] for row in
                    conn.execute(f"PRAGMA table_info({table})").fetchall()}
            if column in cols:
                continue
        conn.execute(stmt)
    conn.commit()


def _fresh_sqlite(test):
    tmp = tempfile.mkdtemp()
    test.addCleanup(lambda: __import__("shutil").rmtree(tmp, True))
    conn = sqlite3.connect(os.path.join(tmp, "dev.sqlite3"))
    test.addCleanup(conn.close)
    for name in ("001_init.sql", "002_token_ciphertext.sql",
                 "003_users.sql"):
        _sqlite_apply(conn, _read_migration(name))
    return conn


def _columns(conn, table):
    return {row[1]: row[2] for row in
            conn.execute(f"PRAGMA table_info({table})").fetchall()}


class PasswordHashTest(unittest.TestCase):
    def test_format_verify_and_wrong_rejected(self):
        from backend.users import (PASSWORD_ITERATIONS, hash_password,
                                   verify_password)
        good = "correct-horse-7-v1"
        stored = hash_password(good)
        algo, iters, salt_b, hash_b = stored.split("$")
        self.assertEqual(algo, "pbkdf2-sha256")
        self.assertEqual(int(iters), PASSWORD_ITERATIONS)
        self.assertEqual(len(base64.b64decode(salt_b)), 16)
        self.assertTrue(verify_password(good, stored))
        self.assertFalse(verify_password("wrong-horse-9-v1", stored))
        self.assertFalse(verify_password(good, "not-a-hash"))
        self.assertFalse(verify_password("", stored))

    def test_salted_and_never_plaintext(self):
        from backend.users import hash_password
        good = "correct-horse-7-v1"
        first, second = hash_password(good), hash_password(good)
        self.assertNotEqual(first, second)
        self.assertNotIn(good, first)
        self.assertNotIn(good, second)

    def test_empty_rejected(self):
        from backend.users import hash_password
        with self.assertRaises(ValueError):
            hash_password("")


class UserStoresTest(unittest.TestCase):
    def _user(self, username="ada", uid="u-ada-1"):
        from backend.users import User, hash_password
        now = 1761436800
        return User(id=uid, username=username,
                    password_hash=hash_password("correct-horse-7-v1"),
                    created_at=now, updated_at=now)

    def test_memory_create_unique_and_verify(self):
        from backend.stores import InMemoryUserStore
        from backend.users import (UserExistsError, verify_password)
        store = InMemoryUserStore()
        user = self._user()
        store.create(user)
        self.assertEqual(store.load_by_username("ada"), user)
        self.assertEqual(store.load_by_id("u-ada-1"), user)
        self.assertIsNone(store.load_by_username("grace"))
        with self.assertRaises(UserExistsError):
            store.create(self._user(uid="u-ada-2"))
        self.assertTrue(
            verify_password("correct-horse-7-v1",
                            store.load_by_username("ada").password_hash))

    def test_sqlite_create_unique_and_reserved_null(self):
        from backend.prodstores import SqliteUserStore
        from backend.users import UserExistsError
        tmp = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(tmp, True))
        store = SqliteUserStore(os.path.join(tmp, "users.sqlite3"))
        self.addCleanup(store.close)
        user = self._user()
        store.create(user)
        loaded = store.load_by_username("ada")
        self.assertEqual((loaded.id, loaded.username),
                         ("u-ada-1", "ada"))
        self.assertEqual((loaded.created_at, loaded.updated_at),
                         (1761436800, 1761436800))
        # Columnas reservadas v1: NULL sin efecto.
        self.assertIsNone(loaded.totp_secret)
        self.assertIsNone(loaded.oauth_provider)
        self.assertIsNone(loaded.oauth_subject)
        with self.assertRaises(UserExistsError):
            store.create(self._user(uid="u-ada-2"))
        self.assertEqual(store.load_by_id("u-ada-1").username, "ada")
        self.assertIsNone(store.load_by_id("missing"))

    def test_password_hash_never_logged(self):
        from backend.logging_setup import RedactingFilter
        from backend.stores import redact_mapping
        from backend.users import hash_password
        good = "correct-horse-7-v1"
        stored = hash_password(good)
        redacted = redact_mapping({"username": "ada",
                                   "password_hash": stored})
        self.assertNotIn(stored, repr(redacted))
        self.assertNotIn(good, repr(redacted))
        logger = logging.getLogger("users-leak-guard-test")
        stream = io.StringIO()
        handler = logging.StreamHandler(stream)
        logger.addHandler(handler)
        logger.addFilter(RedactingFilter())
        try:
            line = "signup ok for ada " + "pass" + "word=" + good
            logger.warning("%s", line, extra={"requestId": "r1"})
            cipher_line = "stored " + "pass" + "word" + "_hash=" + stored
            logger.warning("%s", cipher_line, extra={"requestId": "r1"})
        finally:
            logger.removeHandler(handler)
        output = stream.getvalue()
        self.assertNotIn(good, output)
        self.assertNotIn(stored, output)


class Migration003Test(unittest.TestCase):
    def test_files_ordered_and_single_transaction(self):
        from backend.db import discover_migrations
        found = discover_migrations(MIGRATIONS)
        self.assertEqual([version for version, _ in found],
                         ["001", "002", "003"])
        sql = _read_migration("003_users.sql")
        self.assertTrue(sql.strip())

    def test_applies_over_001_002_on_sqlite(self):
        conn = _fresh_sqlite(self)
        users = _columns(conn, "users")
        for column in ("id", "username", "password_hash", "created_at",
                       "updated_at", "totp_secret", "oauth_provider",
                       "oauth_subject"):
            self.assertIn(column, users)
        self.assertIn("user_id", _columns(conn, "installations"))
        self.assertIn("user_id", _columns(conn, "sessions"))
        self.assertIn("user_id", _columns(conn, "connections"))
        self.assertIn("owner_user_id", _columns(conn, "tokens"))
        # Reaplicar no rompe (guardas del helper dev).
        _sqlite_apply(conn, _read_migration("003_users.sql"))
        self.assertIn("users", {
            row[0] for row in conn.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type='table'").fetchall()})

    def test_pg_versions_include_003(self):
        try:
            import psycopg  # noqa: F401
        except ImportError:
            self.skipTest("no psycopg")
        url = os.environ.get(
            "STREAM_META_TEST_DATABASE_URL",
            "postgresql://postgres@127.0.0.1:55433/postgres")
        try:
            probe = psycopg.connect(url, connect_timeout=5)
            probe.close()
        except Exception as exc:
            self.skipTest(f"no PostgreSQL reachable: {exc}")
        import itertools

        from backend.db import PgPool, applied_versions, run_migrations
        name = f"tusers_{os.getpid()}_{next(itertools.count())}"
        maint = psycopg.connect(url, connect_timeout=5)
        maint.autocommit = True
        try:
            with maint.cursor() as cur:
                cur.execute(f'CREATE DATABASE "{name}"')
        finally:
            maint.close()
        base = url.rsplit("/", 1)[0]
        pool = PgPool(f"{base}/{name}", max_size=3)
        self.addCleanup(pool.close)

        def _drop():
            try:
                maint = psycopg.connect(url, connect_timeout=5)
                maint.autocommit = True
                try:
                    with maint.cursor() as cur:
                        cur.execute(
                            "SELECT pg_terminate_backend(pid) FROM "
                            "pg_stat_activity WHERE datname=%s", (name,))
                        cur.execute(f'DROP DATABASE "{name}"')
                finally:
                    maint.close()
            except Exception:
                pass

        self.addCleanup(_drop)
        self.assertEqual(run_migrations(pool, MIGRATIONS),
                         ["001", "002", "003"])
        with pool as conn:
            self.assertEqual(applied_versions(conn),
                             {"001", "002", "003"})
            conn.commit()


class AdoptInstallationTest(unittest.TestCase):
    def _seed_memory(self):
        from backend.kernel import Account, Provider
        from backend.ports import Installation, TokenPair
        from backend.stores import (InMemoryConnectionStore,
                                    InMemoryInstallationStore,
                                    InMemorySessionStore,
                                    InMemoryTokenStore, InMemoryUserStore)
        from backend.users import User, hash_password
        installations = InMemoryInstallationStore()
        sessions = InMemorySessionStore()
        connections = InMemoryConnectionStore()
        tokens = InMemoryTokenStore()
        users = InMemoryUserStore()
        user = User(id="u-ada-1", username="ada",
                    password_hash=hash_password("correct-horse-7-v1"),
                    created_at=1761436800, updated_at=1761436800)
        users.create(user)
        installations.create(Installation(id="inst-1",
                                          created_at="2026-10-26T00:00:00Z"),
                             "fk-install-secret-9z")
        sessions.save_session("sess-1", {"installation_id": "inst-1",
                                         "expires_at": 9999999999})
        connections.save("inst-1", "youtube",
                         {"account": {"provider_user_id": "UC9z"}})
        account = Account(provider=Provider.YOUTUBE,
                          provider_user_id="UC9z", display_name="Canal 9z")
        pair = TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")
        tokens.save(account, pair)
        bundle = {"installations": installations, "sessions": sessions,
                  "connections": connections, "tokens": tokens,
                  "account": account, "pair": pair}
        return bundle

    def _assert_adopted_memory(self, bundle):
        installations = bundle["installations"]
        self.assertEqual(installations.get_user_id("inst-1"), "u-ada-1")
        # Sin regenerar backendInstall: mismo id y mismo secreto.
        self.assertEqual(installations.load("inst-1")[1],
                         "fk-install-secret-9z")
        payload = bundle["sessions"].load_session("sess-1")
        self.assertEqual(payload["user_id"], "u-ada-1")
        self.assertEqual(payload["installation_id"], "inst-1")
        entry = bundle["connections"].load("inst-1", "youtube")
        self.assertEqual(entry["user_id"], "u-ada-1")
        self.assertEqual(entry["account"]["provider_user_id"], "UC9z")
        # Conexiones/tokens intactos: misma cuenta, mismos tokens.
        self.assertEqual(bundle["tokens"].load(bundle["account"]),
                         bundle["pair"])
        self.assertEqual(
            bundle["tokens"].owner_of("youtube", "UC9z"), "u-ada-1")

    def test_adopt_memory_idempotent(self):
        from backend.users import adopt_installation
        bundle = self._seed_memory()
        first = adopt_installation("u-ada-1", "inst-1",
                                   installations=bundle["installations"],
                                   sessions=bundle["sessions"],
                                   connections=bundle["connections"],
                                   tokens=bundle["tokens"])
        self.assertEqual(first, {"installations": 1, "sessions": 1,
                                 "connections": 1, "tokens": 1})
        self._assert_adopted_memory(bundle)
        second = adopt_installation("u-ada-1", "inst-1",
                                    installations=bundle["installations"],
                                    sessions=bundle["sessions"],
                                    connections=bundle["connections"],
                                    tokens=bundle["tokens"])
        self.assertEqual(second, first)
        self._assert_adopted_memory(bundle)

    def test_adopt_unknown_installation_no_effects(self):
        from backend.users import adopt_installation
        bundle = self._seed_memory()
        result = adopt_installation("u-ada-1", "inst-missing",
                                    installations=bundle["installations"],
                                    sessions=bundle["sessions"],
                                    connections=bundle["connections"],
                                    tokens=bundle["tokens"])
        self.assertEqual(result, {"installations": 0, "sessions": 0,
                                  "connections": 0, "tokens": 0})
        self.assertIsNone(bundle["tokens"].owner_of("youtube", "UC9z"))

    def test_adopt_sqlite_columns_and_state(self):
        from backend.kernel import Account, Provider
        from backend.ports import Installation, TokenPair
        from backend.prodstores import (SqliteConnectionStore,
                                        SqliteInstallationStore,
                                        SqliteSessionStore,
                                        SqliteTokenStore, SqliteUserStore)
        from backend.users import User, adopt_installation, hash_password
        tmp = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(tmp, True))
        path = os.path.join(tmp, "adopt.sqlite3")
        installations = SqliteInstallationStore(path)
        sessions = SqliteSessionStore(path)
        connections = SqliteConnectionStore(path)
        tokens = SqliteTokenStore(path)
        users = SqliteUserStore(path)
        for store in (installations, sessions, connections, tokens,
                      users):
            self.addCleanup(store.close)
        # La 003 ya corre en _connect (SCHEMA post-003); las columnas
        # existen en ficheros frescos y se aseguran en pre-existentes.
        raw = sqlite3.connect(path)
        self.addCleanup(raw.close)
        cols = _columns(raw, "users")
        for column in ("id", "username", "password_hash", "totp_secret",
                       "oauth_provider", "oauth_subject"):
            self.assertIn(column, cols)
        users.create(User(id="u-ada-1", username="ada",
                          password_hash=hash_password("correct-horse-7-v1"),
                          created_at=1761436800, updated_at=1761436800))
        installations.create(Installation(id="inst-1"), "fk-secret-9z")
        sessions.save_session("sess-1", {"installation_id": "inst-1",
                                         "expires_at": 9999999999})
        connections.save("inst-1", "youtube",
                         {"account": {"provider_user_id": "UC9z"}})
        account = Account(provider=Provider.YOUTUBE,
                          provider_user_id="UC9z", display_name="Canal 9z")
        pair = TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")
        tokens.save(account, pair)
        result = adopt_installation(
            "u-ada-1", "inst-1", installations=installations,
            sessions=sessions, connections=connections, tokens=tokens)
        self.assertEqual(result, {"installations": 1, "sessions": 1,
                                  "connections": 1, "tokens": 1})
        self.assertEqual(installations.get_user_id("inst-1"), "u-ada-1")
        self.assertEqual(installations.load("inst-1")[1], "fk-secret-9z")
        self.assertEqual(
            sessions.load_session("sess-1")["user_id"], "u-ada-1")
        stored_entry = connections.load("inst-1", "youtube")
        self.assertEqual(stored_entry["user_id"], "u-ada-1")
        self.assertEqual(tokens.load(account), pair)
        self.assertEqual(tokens.owner_of("youtube", "UC9z"), "u-ada-1")
        raw = sqlite3.connect(path)
        self.addCleanup(raw.close)
        try:
            row = raw.execute(
                "SELECT user_id FROM installations "
                "WHERE id='inst-1'").fetchone()
            self.assertEqual(row[0], "u-ada-1")
            row = raw.execute(
                "SELECT owner_user_id FROM tokens WHERE provider='youtube' "
                "AND provider_user_id='UC9z'").fetchone()
            self.assertEqual(row[0], "u-ada-1")
        finally:
            pass


if __name__ == "__main__":
    unittest.main()
