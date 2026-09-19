"""Endpoint F-C4 POST /ops/purge (T-065): auth de operador + conteos.

TDD: empieza en rojo (BackendApp sin `secrets` ni `ops_purge`) y pasa con
el minimo codigo productivo. Fakes sinteticos, ningun secreto real.
"""
import unittest

from backend.config import Settings
from backend.http_server import BackendApp
from backend.stores import (AllowAllRateLimiter, InMemoryOAuthTransactionStore,
                            InMemorySessionStore)


class _Secrets:
    def __init__(self, token=None):
        self._token = token

    def get(self, name):
        return self._token if name == "OPS_PURGE_TOKEN" else None


def _app(token="fk-ops-9z"):
    settings = Settings()
    sessions = InMemorySessionStore()
    sessions.save_session("old", {"installation_id": "i",
                                  "expires_at": 1.0})
    sessions.save_session("live", {"installation_id": "i",
                                   "expires_at": 9999999999.0})
    txns = InMemoryOAuthTransactionStore()
    txns.save({"id": "used", "installation_id": "i",
               "expires_at": 9999999999.0, "consumed": True})
    return BackendApp(settings=settings, sessions=sessions,
                      limiter=AllowAllRateLimiter(),
                      transactions=txns, secrets=_Secrets(token))


class OpsPurgeTest(unittest.TestCase):
    def test_unauthorized_without_bearer(self):
        from backend.errors import ErrorCode
        app = _app()
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)

    def test_wrong_bearer_rejected(self):
        from backend.errors import ErrorCode
        app = _app()
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("fk-ops-wrong")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)

    def test_unconfigured_returns_500(self):
        from backend.errors import ErrorCode
        app = _app(token=None)
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("fk-ops-9z")
        self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)
        self.assertEqual(ctx.exception.http_status(), 500)

    def test_purge_counts_and_keeps_live(self):
        app = _app()
        out = app.ops_purge("fk-ops-9z")
        self.assertEqual(out["purged"]["sessions"], {"expired": 1,
                                                    "revoked": 0})
        self.assertEqual(out["purged"]["transactions"], {"expired": 0,
                                                        "consumed": 1})
        self.assertIsNotNone(app.sessions.load_session("live"))
        # Re-ejecutable: ceros.
        out2 = app.ops_purge("fk-ops-9z")
        self.assertEqual(out2["purged"]["sessions"], {"expired": 0,
                                                     "revoked": 0})

    def test_ops_rate_limit(self):
        from backend.errors import ErrorCode
        from backend.stores import FixedWindowRateLimiter
        app = _app()
        app.limiters["ops"] = FixedWindowRateLimiter(2, 60)
        app.ops_purge("fk-ops-9z")
        app.ops_purge("fk-ops-9z")
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("fk-ops-9z")
        self.assertEqual(ctx.exception.code, ErrorCode.RATE_LIMITED)


if __name__ == "__main__":
    unittest.main()
