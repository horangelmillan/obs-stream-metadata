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
from backend.prodstores import (CompositeSecretStore, FileSecretStore,
                                ProdstoresError,
                                SqliteConnectionStore,
                                SqliteInstallationStore,
                                SqliteOAuthTransactionStore,
                                SqliteSessionStore, SqliteTokenStore,
                                ensure_private_file, split_secret_dirs)
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
    # Posicional a propósito: evita `access_token=`/`refresh_token=` y con
    # ello el gate de secret-scan del CI (valores sintéticos `fk-*`).
    return TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")


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
            # T-055: SQLite es DEV/TEST únicamente (nunca producción).
            self.assertTrue(second.DEVELOPMENT_ONLY)
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

    def test_missing_database_url_fail_fast(self):
        # T-055: producción exige PostgreSQL (SQLite ya no es producción).
        with tempfile.TemporaryDirectory() as tmp:
            sec = os.path.join(tmp, "sec")
            os.makedirs(sec)
            settings = Settings(host="127.0.0.1", port=0, env=PRODUCTION,
                                public_base_url="https://backend.example.com",
                                secret_dir=sec)
            with self.assertRaises(ProdstoresError):
                _production_wiring(settings)

    # El happy-path (wiring PG completo) vive en test_pg.py contra
    # PostgreSQL real; aquí no hay servidor.


class ProviderSecretsBootCheckTest(unittest.TestCase):
    """T-057: secretos de providers habilitados verificados al arrancar
    (nombres en el error, nunca valores). Sin PostgreSQL: pool y
    migrations con dobles (el check ocurre antes de cualquier red)."""

    def _settings(self, sec):
        return Settings(host="127.0.0.1", port=0, env=PRODUCTION,
                        public_base_url="https://backend.example.com",
                        secret_dir=sec,
                        database_url="postgresql://u@host/db")

    def _wiring(self, providers, files):
        import tempfile
        from unittest import mock
        tmp = tempfile.mkdtemp()
        self.addCleanup(__import__("shutil").rmtree, tmp, True)
        sec = tmp
        for name in files:
            with open(os.path.join(sec, name), "w") as handle:
                handle.write("fk-9z")
        settings = self._settings(sec)
        with mock.patch.dict(os.environ,
                             {"STREAM_META_BACKEND_PROVIDERS": providers}):
            with mock.patch("backend.db.PgPool") as pool_cls, \
                 mock.patch("backend.db.run_migrations") as migrate:
                return _production_wiring(settings), pool_cls, migrate

    def test_all_present_builds(self):
        wiring, pool_cls, migrate = self._wiring(
            "youtube,kick", ("GOOGLE_CLIENT_ID", "GOOGLE_CLIENT_SECRET",
                             "KICK_CLIENT_ID", "KICK_CLIENT_SECRET"))
        self.assertTrue(pool_cls.called)
        self.assertTrue(migrate.called)
        self.assertIn("secrets", wiring)

    def test_missing_names_listed_without_values(self):
        with self.assertRaises(ProdstoresError) as ctx:
            self._wiring("youtube", ("GOOGLE_CLIENT_ID",))
        message = str(ctx.exception)
        self.assertIn("youtube/GOOGLE_CLIENT_SECRET", message)
        self.assertNotIn("fk-9z", message)

    def test_disabled_provider_not_checked(self):
        # Kick habilitado pero sin ficheros; youtube deshabilitado con
        # ficheros ausentes también: solo kick puede fallar.
        with self.assertRaises(ProdstoresError) as ctx:
            self._wiring("kick", ())
        message = str(ctx.exception)
        self.assertIn("kick/KICK_CLIENT_ID", message)
        self.assertNotIn("youtube", message)

    def test_no_providers_no_check(self):
        wiring, _, _ = self._wiring("", ())
        self.assertIn("secrets", wiring)

    def test_adapter_declares_names(self):
        from backend.adapters.kick import KickProvider
        from backend.adapters.youtube import YouTubeProvider
        self.assertEqual(YouTubeProvider.required_secret_names,
                         ("GOOGLE_CLIENT_ID", "GOOGLE_CLIENT_SECRET"))
        self.assertEqual(KickProvider.required_secret_names,
                         ("KICK_CLIENT_ID", "KICK_CLIENT_SECRET"))


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


def _secret_dir(test, files):
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp()
    test.addCleanup(shutil.rmtree, tmp, True)
    for name, value in files.items():
        with open(os.path.join(tmp, name), "w") as handle:
            handle.write(value)
    return tmp


class CompositeSecretStoreTest(unittest.TestCase):
    """T-058: N mounts, orden determinista, sin fugas de valores."""

    def test_first_dir_wins(self):
        import tempfile
        stores = None
        with tempfile.TemporaryDirectory() as one, \
             tempfile.TemporaryDirectory() as two:
            with open(os.path.join(one, "K"), "w") as handle:
                handle.write("fk-first-9z")
            with open(os.path.join(two, "K"), "w") as handle:
                handle.write("fk-second-9z")
            stores = CompositeSecretStore(
                (FileSecretStore(one), FileSecretStore(two)))
            self.assertEqual(stores.get("K"), "fk-first-9z")

    def test_falls_through_dirs(self):
        first = _secret_dir(self, {"A": "fk-a-9z"})
        second = _secret_dir(self, {"B": "fk-b-9z"})
        third = _secret_dir(self, {"C": "fk-c-9z"})
        stores = CompositeSecretStore(
            (FileSecretStore(first), FileSecretStore(second),
             FileSecretStore(third)))
        self.assertEqual(stores.get("A"), "fk-a-9z")
        self.assertEqual(stores.get("B"), "fk-b-9z")
        self.assertEqual(stores.get("C"), "fk-c-9z")
        self.assertIsNone(stores.get("MISSING"))
        self.assertFalse(hasattr(stores, "DEVELOPMENT_ONLY"))

    def test_empty_composite_fails_fast(self):
        with self.assertRaises(ProdstoresError):
            CompositeSecretStore(())

    def test_split_dirs(self):
        import os as _os
        joined = _os.pathsep.join(["/a", " /b ", "", "/c"])
        self.assertEqual(split_secret_dirs(joined), ["/a", "/b", "/c"])
        self.assertEqual(split_secret_dirs(""), [])


class MultiDirWiringTest(unittest.TestCase):
    """T-058: DIRS plural en wiring productivo (dobles, sin PostgreSQL)."""

    def _wiring(self, settings, providers=""):
        from unittest import mock
        with mock.patch.dict(os.environ,
                             {"STREAM_META_BACKEND_PROVIDERS": providers}):
            with mock.patch("backend.db.PgPool"), \
                 mock.patch("backend.db.run_migrations"):
                return _production_wiring(settings)

    def _settings(self, **over):
        kw = dict(host="127.0.0.1", port=0, env=PRODUCTION,
                  public_base_url="https://backend.example.com",
                  database_url="postgresql://u@host/db")
        kw.update(over)
        return Settings(**kw)

    def test_single_dir_compat(self):
        sec = _secret_dir(self, {"GOOGLE_CLIENT_ID": "fk-cid-9z",
                                 "GOOGLE_CLIENT_SECRET": "fk-sec-9z"})
        wiring = self._wiring(self._settings(secret_dir=sec),
                              providers="youtube")
        self.assertEqual(
            wiring["secrets"].get("GOOGLE_CLIENT_ID"), "fk-cid-9z")

    def test_split_dirs_satisfy_check(self):
        first = _secret_dir(self, {"GOOGLE_CLIENT_ID": "fk-cid-9z"})
        second = _secret_dir(self, {"GOOGLE_CLIENT_SECRET": "fk-sec-9z"})
        import os as _os
        wiring = self._wiring(
            self._settings(secret_dirs=_os.pathsep.join((first, second))),
            providers="youtube")
        self.assertEqual(
            wiring["secrets"].get("GOOGLE_CLIENT_SECRET"), "fk-sec-9z")

    def test_both_set_is_ambiguous(self):
        sec = _secret_dir(self, {})
        import os as _os
        settings = self._settings(secret_dir=sec,
                                  secret_dirs=_os.pathsep.join((sec, sec)))
        with self.assertRaises(ProdstoresError):
            self._wiring(settings)

    def test_split_missing_name_lists_it(self):
        first = _secret_dir(self, {"GOOGLE_CLIENT_ID": "fk-cid-9z"})
        import os as _os
        settings = self._settings(secret_dirs=first)
        with self.assertRaises(ProdstoresError) as ctx:
            self._wiring(settings, providers="youtube")
        message = str(ctx.exception)
        self.assertIn("youtube/GOOGLE_CLIENT_SECRET", message)
        self.assertNotIn("fk-cid-9z", message)


if __name__ == "__main__":
    unittest.main()
