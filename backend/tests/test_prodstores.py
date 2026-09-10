"""Tests T-054: stores productivos + wiring de producción.

Garantías:
- FileSecretStore lee secretos de ficheros (sin env, sin repo); dir
  ausente = fail-fast; traversal = None.
- SQLite stores hacen roundtrip de cada port y sobreviven a reapertura
  (reinicio del proceso); 0600 enforced en POSIX.
- main()-level: _production_wiring exige data_dir + secret_dir.
- serve() con TLS a medias (solo cert o ficheros ausentes) = fail-fast.
- Producción con prodstores + https + limiter explícito = aceptada.
- Solo valores sintéticos; ningún secreto real.

Nota Windows: los stores se cierran con try/finally antes de borrar el
tempdir (el SO bloquea ficheros abiertos).
"""
import os
import shutil
import stat
import tempfile
import unittest

from backend.app import _production_wiring, create_app
from backend.config import Settings
from backend.environment import PRODUCTION
from backend.http_server import serve
from backend.kernel import Account, Provider
from backend.ports import Installation, TokenPair
from backend.prodstores import (FileSecretStore, ProdstoresError,
                                SqliteConnectionStore,
                                SqliteInstallationStore,
                                SqliteOAuthTransactionStore,
                                SqliteSessionStore, SqliteTokenStore,
                                ensure_private_file)
from backend.stores import FixedWindowRateLimiter


def _mktemp(test):
    tmp = tempfile.mkdtemp()
    test.addCleanup(shutil.rmtree, tmp, True)
    return tmp


def _db(tmp):
    return os.path.join(tmp, "meta.db")


def _account():
    return Account(provider=Provider.YOUTUBE, provider_user_id="UC9z",
                   display_name="Canal 9z", scopes=("s1",))


def _pair():
    return TokenPair(access_token="fk-acc-9z", refresh_token="fk-ref-9z",
                     expires_in=3600, scope="s1")


class FileSecretStoreTest(unittest.TestCase):
    def test_read_and_missing(self):
        tmp = _mktemp(self)
        with open(os.path.join(tmp, "GOOGLE_CLIENT_ID"), "w") as handle:
            handle.write("fk-cid-9z\n")
        store = FileSecretStore(tmp)
        self.assertEqual(store.get("GOOGLE_CLIENT_ID"), "fk-cid-9z")
        self.assertIsNone(store.get("GOOGLE_CLIENT_SECRET"))
        self.assertFalse(hasattr(store, "DEVELOPMENT_ONLY"))

    def test_missing_dir_fails_fast(self):
        with self.assertRaises(ProdstoresError):
            FileSecretStore(os.path.join("no", "existe", "sec-9z"))

    def test_traversal_refused(self):
        tmp = _mktemp(self)
        store = FileSecretStore(tmp)
        self.assertIsNone(store.get("../x"))
        self.assertIsNone(store.get(".hidden"))


class SqliteInstallationsTest(unittest.TestCase):
    def test_roundtrip_revoke_and_restart(self):
        tmp = _mktemp(self)
        db = _db(tmp)
        first = SqliteInstallationStore(db)
        try:
            first.create(Installation(id="i1"), "fk-secret-9z")
            found = first.load("i1")
            self.assertIsNotNone(found)
            self.assertEqual(found[1], "fk-secret-9z")
            self.assertIsNone(first.load("nope"))
            first.revoke("i1")
            self.assertTrue(first.load("i1")[0].revoked)
        finally:
            first.close()
        # Reapertura = supervivencia al reinicio.
        second = SqliteInstallationStore(db)
        try:
            again = second.load("i1")
            self.assertEqual(again[1], "fk-secret-9z")
            self.assertTrue(again[0].revoked)
            self.assertFalse(hasattr(second, "DEVELOPMENT_ONLY"))
        finally:
            second.close()


class SqliteSessionsTest(unittest.TestCase):
    def test_roundtrip_delete(self):
        tmp = _mktemp(self)
        store = SqliteSessionStore(_db(tmp))
        try:
            store.save_session("s1", {"a": 1})
            self.assertEqual(store.load_session("s1"), {"a": 1})
            store.delete_session("s1")
            self.assertIsNone(store.load_session("s1"))
        finally:
            store.close()


class SqliteTransactionsTest(unittest.TestCase):
    def _txn(self, tid="t1", state="st1", exp=9999999999):
        return {"id": tid, "provider": "youtube", "installation_id": "i1",
                "state": state, "code_verifier": "v",
                "redirect_uri": "https://x/cb", "created_at": 1,
                "expires_at": exp, "consumed": False}

    def test_consume_single_use_and_state(self):
        tmp = _mktemp(self)
        store = SqliteOAuthTransactionStore(_db(tmp))
        try:
            store.save(self._txn())
            self.assertIsNotNone(store.find_by_state("st1"))
            got = store.consume("t1")
            self.assertEqual(got["id"], "t1")
            self.assertIsNone(store.consume("t1"))
            self.assertIsNone(store.find_by_state("st1"))
        finally:
            store.close()

    def test_expired_is_dropped(self):
        tmp = _mktemp(self)
        store = SqliteOAuthTransactionStore(_db(tmp))
        try:
            store.save(self._txn(exp=1))
            self.assertIsNone(store.consume("t1"))
        finally:
            store.close()


class SqliteConnectionsTokensTest(unittest.TestCase):
    def test_connections_roundtrip(self):
        tmp = _mktemp(self)
        store = SqliteConnectionStore(_db(tmp))
        try:
            entry = {"account": {"provider_user_id": "UC9z"},
                     "tokens": {"refresh_token": "fk-ref-9z"}}
            store.save("i1", "youtube", entry)
            self.assertEqual(store.load("i1", "youtube"), entry)
            self.assertIsNone(store.load("i1", "kick"))
            store.delete("i1", "youtube")
            self.assertIsNone(store.load("i1", "youtube"))
        finally:
            store.close()

    def test_tokens_roundtrip_isolated_by_account(self):
        tmp = _mktemp(self)
        store = SqliteTokenStore(_db(tmp))
        try:
            store.save(_account(), _pair())
            got = store.load(_account())
            self.assertEqual(got.refresh_token, "fk-ref-9z")
            other = Account(provider=Provider.YOUTUBE,
                            provider_user_id="other",
                            display_name="O", scopes=())
            self.assertIsNone(store.load(other))
            store.delete(_account())
            self.assertIsNone(store.load(_account()))
        finally:
            store.close()


class PrivateFileTest(unittest.TestCase):
    def test_created_0600_and_world_readable_refused(self):
        tmp = _mktemp(self)
        db = _db(tmp)
        store = SqliteSessionStore(db)
        try:
            if os.name == "posix":
                mode = stat.S_IMODE(os.stat(db).st_mode)
                self.assertEqual(mode, 0o600)
                loose = os.path.join(tmp, "loose.db")
                fd = os.open(loose, os.O_RDWR | os.O_CREAT, 0o644)
                os.close(fd)
                with self.assertRaises(ProdstoresError):
                    ensure_private_file(loose)
            else:
                self.assertTrue(os.path.isfile(db))
        finally:
            store.close()


class ProductionWiringTest(unittest.TestCase):
    def test_missing_dirs_fail_fast(self):
        settings = Settings(host="127.0.0.1", port=0, env=PRODUCTION,
                            public_base_url="https://backend.example.com")
        with self.assertRaises(ProdstoresError):
            _production_wiring(settings)

    def test_prod_wiring_builds_non_dev_stores(self):
        tmp = _mktemp(self)
        data = os.path.join(tmp, "data")
        sec = os.path.join(tmp, "sec")
        os.makedirs(data)
        os.makedirs(sec)
        settings = Settings(host="127.0.0.1", port=0, env=PRODUCTION,
                            public_base_url="https://backend.example.com",
                            data_dir=data, secret_dir=sec)
        wiring = _production_wiring(settings)
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
            for store in wiring.values():
                close = getattr(store, "close", None)
                if callable(close):
                    close()


class ServeTlsTest(unittest.TestCase):
    def test_half_tls_config_fails_fast(self):
        settings = Settings(host="127.0.0.1", port=0,
                            tls_certfile="/no/cert.pem", tls_keyfile="")
        app = create_app(settings=settings, providers={})
        with self.assertRaises(ProdstoresError):
            serve(app)

    def test_missing_tls_files_fail_fast(self):
        settings = Settings(host="127.0.0.1", port=0,
                            tls_certfile="/no/cert.pem",
                            tls_keyfile="/no/key.pem")
        app = create_app(settings=settings, providers={})
        with self.assertRaises(ProdstoresError):
            serve(app)


if __name__ == "__main__":
    unittest.main()
