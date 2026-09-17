"""Tests F-C1 (T-063): AEAD de tokens en reposo (Fernet + Secret Manager).

Disciplina secret-scan del CI: TokenPair siempre posicional con valores
sinteticos `fk-*` (jamas `access_token="..."` literales); claves Fernet
generadas en runtime via `Fernet.generate_key()`, nunca literales.
"""
import unittest

from backend.kernel import Account, Provider
from backend.ports import TokenPair
from backend.stores import InMemoryTokenStore
from backend.token_crypto import (CIPHERTEXT_PREFIX, EncryptedTokenStore,
                                  TokenCipher, TokenCryptoError,
                                  needs_upgrade)


def _account(uid="UC9z"):
    return Account(provider=Provider.YOUTUBE, provider_user_id=uid,
                   display_name="Canal 9z", scopes=("s1",))


def _pair():
    # Posicional a proposito (ver docstring del modulo).
    return TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")


def _cipher():
    from cryptography.fernet import Fernet
    return TokenCipher(Fernet.generate_key())


def _wrapped():
    return EncryptedTokenStore(InMemoryTokenStore(), _cipher())


class CipherRoundtripTest(unittest.TestCase):
    def test_roundtrip(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        store.save(_account(), _pair())
        raw = inner.load(_account())
        self.assertTrue(raw.access_token.startswith(CIPHERTEXT_PREFIX))
        self.assertTrue(raw.refresh_token.startswith(CIPHERTEXT_PREFIX))
        self.assertNotIn("fk-acc-9z", raw.access_token)
        self.assertNotIn("fk-ref-9z", raw.refresh_token)
        got = store.load(_account())
        self.assertEqual(got.access_token, "fk-acc-9z")
        self.assertEqual(got.refresh_token, "fk-ref-9z")
        self.assertEqual(got.expires_in, 3600)
        self.assertEqual(got.scope, "s1")

    def test_wrong_key_fails_closed(self):
        store = _wrapped()
        store.save(_account(), _pair())
        from cryptography.fernet import Fernet
        other = EncryptedTokenStore(store._inner,
                                    TokenCipher(Fernet.generate_key()))
        with self.assertRaises(TokenCryptoError):
            other.load(_account())

    def test_tampered_cell_fails_closed(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        store.save(_account(), _pair())
        raw = inner.load(_account())
        tampered = raw.access_token[:-4] + "AAAA"
        inner.save(_account(), TokenPair(tampered, raw.refresh_token, 3600,
                                         "s1"))
        with self.assertRaises(TokenCryptoError):
            store.load(_account())

    def test_empty_refresh_stays_empty(self):
        # `refresh_token` ausente (p. ej. YouTube sin offline) no se cifra:
        # conserva la semantica que `ensure_fresh_token` espera (falsy ->
        # SESSION_EXPIRED, nunca un bearer inventado).
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        store.save(_account(), TokenPair("fk-acc-9z", "", 3600, "s1"))
        raw = inner.load(_account())
        self.assertEqual(raw.refresh_token, "")
        self.assertEqual(store.load(_account()).refresh_token, "")


class LegacyDualReadTest(unittest.TestCase):
    def test_legacy_plaintext_still_reads(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account(), _pair())  # fila pre-F-C1, en claro
        got = store.load(_account())
        self.assertEqual(got.access_token, "fk-acc-9z")
        self.assertEqual(got.refresh_token, "fk-ref-9z")

    def test_mixed_rows_coexist(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account("legacy"), _pair())
        store.save(_account("nuevo"), _pair())
        self.assertEqual(store.load(_account("legacy")).access_token,
                         "fk-acc-9z")
        self.assertTrue(inner.load(_account("nuevo")).access_token
                        .startswith(CIPHERTEXT_PREFIX))

    def test_save_upgrades_legacy_to_ciphertext(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account(), _pair())
        store.save(_account(), _pair())  # p. ej. refresh tras el deploy
        self.assertTrue(inner.load(_account()).access_token
                        .startswith(CIPHERTEXT_PREFIX))

    def test_missing_returns_none(self):
        self.assertIsNone(_wrapped().load(_account("nadie")))

    def test_delete_delegates(self):
        store = _wrapped()
        store.save(_account(), _pair())
        store.delete(_account())
        self.assertIsNone(store.load(_account()))


class RotationTest(unittest.TestCase):
    def test_previous_key_reads_and_resave_upgrades(self):
        from cryptography.fernet import Fernet
        old_raw, new_raw = Fernet.generate_key(), Fernet.generate_key()
        old_store = EncryptedTokenStore(InMemoryTokenStore(),
                                        TokenCipher(old_raw))
        old_store.save(_account(), _pair())
        inner = old_store._inner
        rotated = EncryptedTokenStore(inner, TokenCipher(new_raw, old_raw))
        self.assertEqual(rotated.load(_account()).access_token, "fk-acc-9z")
        rotated.save(_account(), _pair())
        solo_new = EncryptedTokenStore(inner, TokenCipher(new_raw))
        self.assertEqual(solo_new.load(_account()).refresh_token, "fk-ref-9z")


class KeyLoadingTest(unittest.TestCase):
    def test_from_secret_store_and_missing_fails_fast(self):
        import tempfile
        from cryptography.fernet import Fernet

        from backend.prodstores import FileSecretStore, ProdstoresError
        with tempfile.TemporaryDirectory() as tmp:
            with open(tmp + "/TOKEN_ENCRYPTION_KEY", "w") as handle:
                handle.write(Fernet.generate_key().decode())
            cipher = TokenCipher.from_secret_store(FileSecretStore(tmp))
            self.assertEqual(cipher.decrypt(cipher.encrypt("fk-x-9z")),
                             "fk-x-9z")
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ProdstoresError) as ctx:
                TokenCipher.from_secret_store(FileSecretStore(tmp))
            self.assertIn("TOKEN_ENCRYPTION_KEY", str(ctx.exception))

    def test_from_secret_store_with_previous(self):
        import tempfile
        from cryptography.fernet import Fernet

        from backend.prodstores import FileSecretStore
        with tempfile.TemporaryDirectory() as tmp:
            with open(tmp + "/TOKEN_ENCRYPTION_KEY", "w") as handle:
                handle.write(Fernet.generate_key().decode())
            with open(tmp + "/TOKEN_ENCRYPTION_KEY_PREVIOUS", "w") as handle:
                handle.write(Fernet.generate_key().decode())
            cipher = TokenCipher.from_secret_store(FileSecretStore(tmp))
            self.assertEqual(cipher.decrypt(cipher.encrypt("fk-x-9z")),
                             "fk-x-9z")

    def test_malformed_key_fails_fast(self):
        with self.assertRaises(TokenCryptoError):
            TokenCipher("no-es-una-clave")


class FormatHelperTest(unittest.TestCase):
    def test_needs_upgrade(self):
        self.assertTrue(needs_upgrade("fk-acc-9z"))
        self.assertFalse(needs_upgrade(""))
        self.assertFalse(needs_upgrade(CIPHERTEXT_PREFIX + "AAA"))


if __name__ == "__main__":
    unittest.main()
