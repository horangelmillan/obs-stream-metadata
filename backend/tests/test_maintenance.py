"""Purga F-C4 (T-065): solo filas inutilizables; vigentes intactas.

TDD: empieza en rojo (purge_expired inexistente) y pasa con el minimo
codigo productivo. Sin secretos reales (fakes sinteticos).
"""
import unittest

from backend.stores import (InMemoryOAuthTransactionStore,
                            InMemorySessionStore)


class InMemorySessionPurgeTest(unittest.TestCase):
    def test_purge_expired_and_revoked_keeps_live(self):
        store = InMemorySessionStore()
        store.save_session("live", {"installation_id": "i",
                                    "expires_at": 2000.0, "revoked": False})
        store.save_session("old", {"installation_id": "i",
                                   "expires_at": 1000.0, "revoked": False})
        store.save_session("gone", {"installation_id": "i",
                                    "expires_at": 2000.0, "revoked": True})
        store.save_session("both", {"installation_id": "i",
                                    "expires_at": 1000.0, "revoked": True})
        out = store.purge_expired(1500.0)
        self.assertEqual(out, {"expired": 2, "revoked": 1})
        self.assertIsNotNone(store.load_session("live"))
        self.assertIsNone(store.load_session("old"))
        # Re-ejecutable: segunda pasada devuelve ceros.
        self.assertEqual(store.purge_expired(1500.0),
                         {"expired": 0, "revoked": 0})


class InMemoryTransactionPurgeTest(unittest.TestCase):
    def test_purge_expired_and_consumed_keeps_live(self):
        store = InMemoryOAuthTransactionStore()
        store.save({"id": "live", "installation_id": "i",
                    "expires_at": 2000.0, "consumed": False})
        store.save({"id": "old", "installation_id": "i",
                    "expires_at": 1000.0, "consumed": False})
        store.save({"id": "used", "installation_id": "i",
                    "expires_at": 2000.0, "consumed": True})
        out = store.purge_expired(1500.0)
        self.assertEqual(out, {"expired": 1, "consumed": 1})
        self.assertIsNotNone(store.load("live"))
        self.assertEqual(store.purge_expired(1500.0),
                         {"expired": 0, "consumed": 0})


class SqlitePurgeTest(unittest.TestCase):
    def test_sqlite_purge_parity(self):
        import os
        import shutil
        import tempfile
        from backend.prodstores import (SqliteOAuthTransactionStore,
                                        SqliteSessionStore)
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp, True)
        db = os.path.join(tmp, "meta.db")
        sess = SqliteSessionStore(db)
        try:
            sess.save_session("live", {"installation_id": "i",
                                       "expires_at": 2000.0})
            sess.save_session("old", {"installation_id": "i",
                                      "expires_at": 1000.0})
            sess.save_session("gone", {"installation_id": "i",
                                       "expires_at": 2000.0,
                                       "revoked": True})
            self.assertEqual(sess.purge_expired(1500.0),
                             {"expired": 1, "revoked": 1})
            self.assertIsNotNone(sess.load_session("live"))
        finally:
            sess.close()
        txns = SqliteOAuthTransactionStore(db)
        try:
            txns.save({"id": "live", "installation_id": "i",
                       "expires_at": 2000.0, "consumed": False})
            txns.save({"id": "used", "installation_id": "i",
                       "expires_at": 2000.0, "consumed": True})
            self.assertEqual(txns.purge_expired(1500.0),
                             {"expired": 0, "consumed": 1})
        finally:
            txns.close()


class PgPurgeTest(unittest.TestCase):
    def test_pg_purge_parity(self):
        import os
        if not os.environ.get("STREAM_META_TEST_DATABASE_URL"):
            self.skipTest("sin PostgreSQL (diseno: skip local)")
        from backend.pgstores import (PgOAuthTransactionStore,
                                      PgSessionStore)
        from backend.tests.test_pg import fresh_db
        _, pool = fresh_db(self)
        sess, txns = PgSessionStore(pool), PgOAuthTransactionStore(pool)
        sess.save_session("old", {"installation_id": "i",
                                  "expires_at": 1000.0})
        sess.save_session("live", {"installation_id": "i",
                                   "expires_at": 9999999999.0})
        self.assertEqual(sess.purge_expired(1500.0),
                         {"expired": 1, "revoked": 0})
        self.assertIsNotNone(sess.load_session("live"))
        txns.save({"id": "old-t", "installation_id": "i",
                   "expires_at": 1000.0, "consumed": False})
        self.assertEqual(txns.purge_expired(1500.0),
                         {"expired": 1, "consumed": 0})


class PurgeOrchestratorTest(unittest.TestCase):
    def test_orchestrator_counts(self):
        from backend import maintenance as _m
        from backend.tests.test_youtube import FakeClock
        clock = FakeClock(now=1500.0)
        sess = InMemorySessionStore()
        sess.save_session("old", {"installation_id": "i",
                                  "expires_at": 1000.0})
        txns = InMemoryOAuthTransactionStore()
        txns.save({"id": "used", "installation_id": "i",
                   "expires_at": 2000.0, "consumed": True})
        out = _m.purge_expired(sess, txns, clock=clock)
        self.assertEqual(out, {"sessions": {"expired": 1, "revoked": 0},
                               "transactions": {"expired": 0,
                                                "consumed": 1}})


if __name__ == "__main__":
    unittest.main()
