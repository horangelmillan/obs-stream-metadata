"""Tests T-055: PostgreSQL portable real + config DATABASE_URL.

Estrategia:
- Sin mocks del servidor: todo corre contra PostgreSQL de verdad.
- Servidor: `STREAM_META_TEST_DATABASE_URL` si existe; si no, el
  PostgreSQL local de desarrollo (mismo default documentado). Sin
  servidor alcanzable → SkipTest explícito (nunca PASS falso).
- Aislamiento: una base `t055_<pid>_<contador>` por test (CREATE/DROP),
  migrations desde cero en cada una.
- Valores sintéticos `fk-*`; sin credenciales reales; sin `*_token=`
  kwargs (gate de secret-scan del CI).

CI provee servicio postgres:18 + TEST_DATABASE_URL (ver workflow).
"""
import itertools
import os
import unittest

import psycopg

from backend.app import _production_wiring, create_app
from backend.config import Settings, load_settings
from backend.db import (DatabaseError, PgPool, applied_versions,
                        discover_migrations, redacted_url, run_migrations,
                        validate_database_url)
from backend.environment import PRODUCTION
from backend.kernel import Account, Provider
from backend.pgstores import (PgConnectionStore, PgInstallationStore,
                              PgOAuthTransactionStore, PgSessionStore,
                              PgTokenStore)
from backend.ports import Installation, TokenPair
from backend.stores import FixedWindowRateLimiter, redact_text

TEST_URL = os.environ.get(
    "STREAM_META_TEST_DATABASE_URL",
    "postgresql://postgres@127.0.0.1:55433/postgres")
MIGRATIONS = os.path.join(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))), "migrations")
_counter = itertools.count()


def _maintenance():
    return psycopg.connect(TEST_URL, connect_timeout=5)


def require_pg(test):
    try:
        conn = _maintenance()
        conn.close()
    except Exception as exc:
        test.skipTest(f"no PostgreSQL reachable ({TEST_URL!r}): {exc}")
    return True


def fresh_db(test):
    """Crea base efímera migrada desde cero. Devuelve (url, drop)."""
    require_pg(test)
    name = f"t055_{os.getpid()}_{next(_counter)}"
    maint = _maintenance()
    maint.autocommit = True
    try:
        with maint.cursor() as cur:
            cur.execute(f'CREATE DATABASE "{name}"')
    finally:
        maint.close()
    base = TEST_URL.rsplit("/", 1)[0]
    url = f"{base}/{name}"

    def _drop():
        try:
            maint = _maintenance()
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

    test.addCleanup(_drop)
    pool = PgPool(url, max_size=3)
    test.addCleanup(pool.close)
    applied = run_migrations(pool, MIGRATIONS)
    test.assertEqual(applied, ["001"])
    test.assertEqual(run_migrations(pool, MIGRATIONS), [])
    return url, pool


def _account(uid="UC9z"):
    return Account(provider=Provider.YOUTUBE, provider_user_id=uid,
                   display_name="Canal 9z", scopes=("s1",))


def _pair():
    # Posicional: evita kwargs que dispara el secret-scan del CI.
    return TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")


class DatabaseUrlTest(unittest.TestCase):
    def test_valid(self):
        url = "postgresql://u:p@host:5432/db?sslmode=require"
        self.assertEqual(validate_database_url(url), url)

    def test_missing_scheme_host_db_rejected(self):
        for bad in ("", None, "mysql://h/db", "postgresql:///db",
                    "postgresql://host/", "not-a-url"):
            with self.assertRaises(DatabaseError, msg=str(bad)):
                validate_database_url(bad)

    def test_redacted(self):
        self.assertEqual(
            redacted_url("postgresql://u:s3cret@host/db?sslmode=require"),
            "postgresql://u:***@host/db?sslmode=require")
        logged = redact_text(
            "connect postgresql://u:s3cret@host/db failed")
        self.assertNotIn("s3cret", logged)
        self.assertIn("***", logged)

    def test_settings_plumbing(self):
        env = {"STREAM_META_BACKEND_DATABASE_URL":
               "postgresql://u@host/db",
               "STREAM_META_BACKEND_DB_POOL_MAX": "5", "PORT": "9090"}
        settings = load_settings(env)
        self.assertEqual(settings.database_url, "postgresql://u@host/db")
        self.assertEqual(settings.db_pool_max, 5)
        self.assertEqual(settings.port, 9090)


class MigrationsTest(unittest.TestCase):
    def test_discover_ordered(self):
        found = discover_migrations(MIGRATIONS)
        self.assertEqual([version for version, _ in found], ["001"])

    def test_schema_created_from_scratch(self):
        _, pool = fresh_db(self)
        with pool as conn:
            self.assertEqual(applied_versions(conn), {"001"})
            with conn.cursor() as cur:
                cur.execute(
                    "SELECT tablename FROM pg_tables "
                    "WHERE schemaname='public' ORDER BY 1")
                tables = {row[0] for row in cur.fetchall()}
            conn.commit()
        for expected in ("installations", "sessions", "transactions",
                         "connections", "tokens", "schema_migrations"):
            self.assertIn(expected, tables)


class PgStoresTest(unittest.TestCase):
    def test_installations_sessions(self):
        _, pool = fresh_db(self)
        ins, ses = PgInstallationStore(pool), PgSessionStore(pool)
        ins.create(Installation(id="i1"), "fk-secret-9z")
        self.assertEqual(ins.load("i1")[1], "fk-secret-9z")
        self.assertIsNone(ins.load("nope"))
        ins.revoke("i1")
        self.assertTrue(ins.load("i1")[0].revoked)
        ses.save_session("s1", {"n": 2})
        self.assertEqual(ses.load_session("s1"), {"n": 2})
        ses.delete_session("s1")
        self.assertIsNone(ses.load_session("s1"))
        for store in (ins, ses):
            self.assertFalse(getattr(store, "DEVELOPMENT_ONLY", False))

    def test_transactions_single_use(self):
        _, pool = fresh_db(self)
        store = PgOAuthTransactionStore(pool)
        txn = {"id": "t1", "provider": "youtube", "installation_id": "i1",
               "state": "st1", "code_verifier": "v",
               "redirect_uri": "https://x/cb", "created_at": 1,
               "expires_at": 9999999999, "consumed": False}
        store.save(txn)
        self.assertIsNotNone(store.find_by_state("st1"))
        self.assertEqual(store.consume("t1")["id"], "t1")
        self.assertIsNone(store.consume("t1"))
        self.assertIsNone(store.find_by_state("st1"))

    def test_connections_tokens_isolated(self):
        _, pool = fresh_db(self)
        con, tok = PgConnectionStore(pool), PgTokenStore(pool)
        con.save("i1", "youtube", {"a": 1})
        self.assertEqual(con.load("i1", "youtube"), {"a": 1})
        self.assertIsNone(con.load("i1", "kick"))
        tok.save(_account(), _pair())
        self.assertEqual(tok.load(_account()).refresh_token, "fk-ref-9z")
        self.assertIsNone(tok.load(_account("other")))
        tok.delete(_account())
        self.assertIsNone(tok.load(_account()))

    def test_two_pools_see_same_data(self):
        # Evidencia multi-instancia: dos pools = dos "instancias".
        url, pool = fresh_db(self)
        other = PgPool(url, max_size=2)
        self.addCleanup(other.close)
        PgInstallationStore(pool).create(Installation(id="shared"),
                                         "fk-s-9z")
        self.assertEqual(
            PgInstallationStore(other).load("shared")[1], "fk-s-9z")

    def test_pool_exhaustion_fails_fast(self):
        from backend.db import DatabaseError as _DE
        url, pool = fresh_db(self)
        small = PgPool(url, max_size=1, acquire_timeout_s=1)
        self.addCleanup(small.close)
        leaked = small.acquire()
        try:
            with self.assertRaises(_DE):
                small.acquire()
        finally:
            small.release(leaked)


class ProdWiringPgTest(unittest.TestCase):
    def test_production_wiring_over_postgres(self):
        import tempfile
        url, _ = fresh_db(self)
        sec = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(sec, True))
        with open(os.path.join(sec, "GOOGLE_CLIENT_ID"), "w") as handle:
            handle.write("fk-prod-cid-9z")
        settings = Settings(host="127.0.0.1", port=0, env=PRODUCTION,
                            public_base_url="https://backend.example.com",
                            secret_dir=sec, database_url=url)
        # _production_wiring crea su propio pool: cerrarlo al final.
        import backend.db as _dbmod
        pools = []
        real_pool = _dbmod.PgPool

        def _tracking(*args, **kwargs):
            pool = real_pool(*args, **kwargs)
            pools.append(pool)
            return pool

        _dbmod.PgPool = _tracking
        try:
            wiring = _production_wiring(settings)
        finally:
            _dbmod.PgPool = real_pool
        try:
            for role, store in wiring.items():
                if role == "limiter":
                    continue
                self.assertFalse(
                    getattr(store, "DEVELOPMENT_ONLY", False), role)
            app = create_app(settings=settings, providers={},
                             secrets=wiring["secrets"],
                             sessions=wiring["sessions"],
                             installations=wiring["installations"],
                             limiter=wiring["limiter"])
            self.assertEqual(app.version()["env"], PRODUCTION)
        finally:
            for pool in pools:
                pool.close()


if __name__ == "__main__":
    unittest.main()
