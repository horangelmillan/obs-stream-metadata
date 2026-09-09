"""Tests stores: SecretStore/TokenStore/SessionStore + redacción."""
import unittest

from backend.kernel import Account, Provider
from backend.ports import TokenPair
from backend.stores import (AllowAllRateLimiter, EnvSecretStore, InMemorySessionStore,
                            InMemoryTokenStore, redact_mapping, redact_text)


class StoresTest(unittest.TestCase):
    def test_env_secret_store(self):
        store = EnvSecretStore(env={"STREAM_META_BACKEND_SECRET_YT": "s3cr3t"})
        self.assertEqual(store.get("YT"), "s3cr3t")
        self.assertIsNone(store.get("MISSING"))
        self.assertTrue(EnvSecretStore.DEVELOPMENT_ONLY)

    def test_token_store_roundtrip_and_isolation(self):
        store = InMemoryTokenStore()
        a = Account(Provider.YOUTUBE, "u1", "A")
        b = Account(Provider.KICK, "u1", "A")
        store.save(a, TokenPair("at", "rt", 3600, "scope"))
        self.assertIsNone(store.load(b))  # aislamiento por proveedor
        loaded = store.load(a)
        self.assertEqual((loaded.access_token, loaded.refresh_token), ("at", "rt"))
        store.delete(a)
        self.assertIsNone(store.load(a))

    def test_session_store_roundtrip(self):
        store = InMemorySessionStore()
        store.save_session("s1", {"installation": "i1"})
        self.assertEqual(store.load_session("s1"), {"installation": "i1"})
        store.delete_session("s1")
        self.assertIsNone(store.load_session("s1"))

    def test_rate_limiter_hook(self):
        self.assertTrue(AllowAllRateLimiter().allow("anything"))

    def test_redact_mapping_hides_values_keeps_lengths(self):
        canary = "tok-canary-V4LU3"
        out = redact_mapping({"access_token": canary, "provider": "youtube"})
        self.assertNotIn(canary, repr(out))
        self.assertIn(f"len={len(canary)}", out["access_token"])
        self.assertEqual(out["provider"], "youtube")

    def test_redact_text(self):
        self.assertNotIn("abc123", redact_text("client_secret=abc123 grant=x"))


if __name__ == "__main__":
    unittest.main()
