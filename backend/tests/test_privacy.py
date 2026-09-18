"""Tests F-C2 (T-064): borrado total por instalación + privacidad operativa.

TDD: cada clase empieza en rojo (método inexistente) y pasa con el
mínimo código productivo. Sin secretos reales (fakes sintéticos).
"""
import os
import shutil
import tempfile
import unittest

from backend.ports import Installation
from backend.prodstores import (SqliteConnectionStore,
                                SqliteInstallationStore,
                                SqliteOAuthTransactionStore,
                                SqliteSessionStore)
from backend.stores import (InMemoryConnectionStore,
                            InMemoryInstallationStore,
                            InMemoryOAuthTransactionStore,
                            InMemorySessionStore)


def _mktemp(test):
    tmp = tempfile.mkdtemp()
    test.addCleanup(shutil.rmtree, tmp, True)
    return tmp


class StorePurgeTest(unittest.TestCase):
    def test_list_referencing_and_session_purge(self):
        conns = InMemoryConnectionStore()
        conns.save("inst-a", "youtube",
                   {"account": {"provider_user_id": "UC1"}})
        conns.save("inst-b", "youtube",
                   {"account": {"provider_user_id": "UC1"}})
        self.assertEqual(sorted(conns.list_referencing("youtube", "UC1")),
                         ["inst-a", "inst-b"])
        sess = InMemorySessionStore()
        sess.save_session("s1", {"installation_id": "inst-a"})
        sess.save_session("s2", {"installation_id": "inst-b"})
        self.assertEqual(sess.delete_for_installation("inst-a"), 1)
        self.assertIsNotNone(sess.load_session("s2"))

    def test_transaction_purge_and_installation_delete(self):
        txns = InMemoryOAuthTransactionStore()
        txns.save({"id": "t1", "installation_id": "inst-a"})
        txns.save({"id": "t2", "installation_id": "inst-b"})
        self.assertEqual(txns.delete_for_installation("inst-a"), 1)
        self.assertIsNotNone(txns.load("t2"))
        insts = InMemoryInstallationStore()
        insts.create(Installation(id="inst-a"), "secret-a")
        insts.delete("inst-a")
        self.assertIsNone(insts.load("inst-a"))
        insts.delete("missing")  # idempotente


class SqlitePurgeTest(unittest.TestCase):
    def test_sqlite_purge_methods(self):
        tmp = _mktemp(self)
        db = os.path.join(tmp, "meta.db")
        conns = SqliteConnectionStore(db)
        try:
            conns.save("inst-a", "youtube",
                       {"account": {"provider_user_id": "UC1"}})
            conns.save("inst-b", "youtube",
                       {"account": {"provider_user_id": "UC1"}})
            self.assertEqual(conns.list_referencing("youtube", "UC1"),
                             ["inst-a", "inst-b"])
        finally:
            conns.close()
        sess = SqliteSessionStore(db)
        try:
            sess.save_session("s1", {"installation_id": "inst-a"})
            sess.save_session("s2", {"installation_id": "inst-b"})
            self.assertEqual(sess.delete_for_installation("inst-a"), 1)
            self.assertIsNotNone(sess.load_session("s2"))
        finally:
            sess.close()
        txns = SqliteOAuthTransactionStore(db)
        try:
            txns.save({"id": "t1", "installation_id": "inst-a"})
            self.assertEqual(txns.delete_for_installation("inst-a"), 1)
        finally:
            txns.close()
        insts = SqliteInstallationStore(db)
        try:
            insts.create(Installation(id="inst-a"), "fk-secret-9z")
            insts.delete("inst-a")
            self.assertIsNone(insts.load("inst-a"))
        finally:
            insts.close()


class EraseLocalTest(unittest.TestCase):
    def test_disconnect_keeps_shared_token(self):
        from backend.kernel import Account, Provider
        from backend.oauth import ConnectService
        from backend.tests.test_youtube import (
            FakeClock, fake_google_ok, make_service)
        clock = FakeClock()
        conns = InMemoryConnectionStore()
        from backend.stores import (InMemoryOAuthTransactionStore,
                                    InMemoryTokenStore)
        txns = InMemoryOAuthTransactionStore(clock=clock)
        toks = InMemoryTokenStore()
        svc_a, _ = make_service(fake_google_ok, clock=clock)
        svc_a._transactions, svc_a._connections, svc_a._tokens = (
            txns, conns, toks)
        svc_b, _ = make_service(fake_google_ok, clock=clock)
        svc_b._transactions, svc_b._connections, svc_b._tokens = (
            txns, conns, toks)
        for inst in ("inst-a", "inst-b"):
            started = svc_a.start(inst, "http://127.0.0.1:9004/cb")
            txn = txns.load(started["transaction_id"])
            svc_a.callback(txn["state"], "code-1")
        account = Account(Provider.YOUTUBE, "UC123", "Canal Prueba",
                          ("https://www.googleapis.com/auth/youtube.force-ssl",))
        self.assertIsNotNone(toks.load(account))
        svc_a.disconnect("inst-a")
        self.assertEqual(svc_a.status("inst-a")["status"], "disconnected")
        # La instalación superviviente sigue conectada y conserva su fila.
        self.assertEqual(svc_b.status("inst-b")["status"], "connected")
        self.assertIsNotNone(toks.load(account))
        svc_b.disconnect("inst-b")
        self.assertIsNone(toks.load(account))


def _erase_wiring(clock):
    """Dos instalaciones cableadas a stores compartidos (sin red)."""
    from backend.ports import Installation as _Installation
    from backend.stores import (InMemoryInstallationStore,
                                InMemoryOAuthTransactionStore,
                                InMemorySessionStore, InMemoryTokenStore)
    from backend.tests.test_kick import fake_kick_ok
    from backend.tests.test_kick import make_service as make_kick
    from backend.tests.test_youtube import fake_google_ok
    from backend.tests.test_youtube import make_service as make_yt
    revoked = []

    def yt_transport(user_id, title):
        def run(method, url, fields):
            if "revoke" in url:
                revoked.append(("youtube", fields.get("token")))
                return 200, {}
            if "channels" in url:
                return 200, {"items": [{"id": user_id,
                                        "snippet": {"title": title}}]}
            return fake_google_ok(method, url, fields)
        return run

    def kick_transport(method, url, fields):
        if "revoke" in url:
            revoked.append(("kick", fields.get("token")))
            return 200, {}
        return fake_kick_ok(method, url, fields)

    conns = InMemoryConnectionStore()
    toks = InMemoryTokenStore()
    txns = InMemoryOAuthTransactionStore(clock=clock)
    sess = InMemorySessionStore()
    insts = InMemoryInstallationStore()
    services = {}
    for name, maker, transport in (
            ("youtube", make_yt, yt_transport("UC123", "Canal A")),
            ("kick", make_kick, kick_transport)):
        svc, _ = maker(transport, clock=clock)
        svc._transactions, svc._connections, svc._tokens = txns, conns, toks
        services[name] = svc
    # inst-b usa OTRA cuenta de YouTube (UC999) para probar aislamiento.
    svc_b, _ = make_yt(yt_transport("UC999", "Canal B"), clock=clock)
    svc_b._transactions, svc_b._connections, svc_b._tokens = txns, conns, toks

    def connect(svc, inst, redirect):
        started = svc.start(inst, redirect)
        txn = txns.load(started["transaction_id"])
        svc.callback(txn["state"], "code-1")

    connect(services["youtube"], "inst-a", "http://127.0.0.1:9004/cb")
    connect(services["kick"], "inst-a", "http://localhost:3000/cb")
    connect(svc_b, "inst-b", "http://127.0.0.1:9004/cb")
    sess.save_session("s-a", {"installation_id": "inst-a"})
    sess.save_session("s-b", {"installation_id": "inst-b"})
    txns.save({"id": "t-a", "installation_id": "inst-a"})
    insts.create(_Installation(id="inst-a"), "fk-secret-a")
    insts.create(_Installation(id="inst-b"), "fk-secret-b")
    return services, sess, txns, insts, revoked


class EraseInstallationTest(unittest.TestCase):
    def test_erase_removes_own_data_keeps_others(self):
        from backend import privacy as _privacy
        from backend.tests.test_youtube import FakeClock
        clock = FakeClock()
        services, sess, txns, insts, revoked = _erase_wiring(clock)
        out = _privacy.erase_installation("inst-a", services, sess, txns,
                                          insts)
        self.assertEqual(out["erased"],
                         {"connections": 2, "tokens": 2, "sessions": 1,
                          # 2 transacciones de los `start` + 1 manual.
                          "transactions": 3, "installation": 1})
        self.assertEqual(out["revoked"], {"youtube": True, "kick": True})
        self.assertTrue(revoked)
        self.assertEqual(services["youtube"].status("inst-a")["status"],
                         "disconnected")
        self.assertEqual(services["youtube"].status("inst-b")["status"],
                         "connected")
        self.assertIsNotNone(sess.load_session("s-b"))
        self.assertIsNone(insts.load("inst-a"))
        self.assertIsNotNone(insts.load("inst-b"))
        out2 = _privacy.erase_installation("inst-a", services, sess, txns,
                                           insts)
        self.assertEqual(out2["erased"],
                         {"connections": 0, "tokens": 0, "sessions": 0,
                          "transactions": 0, "installation": 0})

    def test_erase_shared_token_revokes_but_keeps_row(self):
        from backend import privacy as _privacy
        from backend.kernel import Account, Provider
        from backend.tests.test_youtube import FakeClock
        clock = FakeClock()
        services, sess, txns, insts, revoked = _erase_wiring(clock)
        # inst-b se reconecta con la MISMA cuenta de inst-a (UC123):
        # misma fila de tokens compartida por dos instalaciones.
        services["youtube"]._connections.delete("inst-b", "youtube")
        started = services["youtube"].start("inst-b",
                                            "http://127.0.0.1:9004/cb")
        txn = txns.load(started["transaction_id"])
        services["youtube"].callback(txn["state"], "code-1")
        account = Account(Provider.YOUTUBE, "UC123", "Canal A",
                          ("https://www.googleapis.com/auth/youtube.force-ssl",))
        out = _privacy.erase_installation("inst-a", services, sess, txns,
                                          insts)
        # Kick (no compartido) sí se borra; YouTube compartido se preserva.
        self.assertEqual(out["erased"]["tokens"], 1)
        yt_toks = services["youtube"]._tokens
        self.assertIsNotNone(yt_toks.load(account))
        self.assertTrue(revoked)  # pero el revoke sí se intentó
        self.assertEqual(services["youtube"].status("inst-b")["status"],
                         "connected")


if __name__ == "__main__":
    unittest.main()
