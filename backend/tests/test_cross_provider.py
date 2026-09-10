"""Tests T-052: aislamiento cross-provider sobre stores compartidos."""
import unittest

from backend.kernel import Account, Provider
from backend.oauth import ConnectService
from backend.ports import TokenPair
from backend.stores import (InMemoryConnectionStore, InMemoryOAuthTransactionStore,
                            InMemoryTokenStore)
from backend.tests.test_adapters import FakeSecrets
from backend.adapters.youtube import YouTubeProvider
from backend.adapters.kick import KickProvider


def _services(clock=None):
    import time as _time
    clock = clock or _time.time
    transactions = InMemoryOAuthTransactionStore(clock=clock)
    connections = InMemoryConnectionStore()
    tokens = InMemoryTokenStore()
    yt = ConnectService(
        YouTubeProvider(FakeSecrets(), "http://127.0.0.1:0/cb"),
        transactions, connections, tokens, clock=clock)
    kk = ConnectService(
        KickProvider(FakeSecrets(), "http://localhost:0/cb"),
        transactions, connections, tokens, clock=clock)
    return yt, kk, tokens


class CrossProviderTest(unittest.TestCase):
    def test_connection_isolation_by_provider(self):
        yt, kk, _ = _services()
        yt._connections.save("inst-1", "youtube", {
            "account": {"provider_user_id": "UC1", "display_name": "C",
                        "scopes": []}, "obtained_at": 0})
        self.assertIsNone(kk.connection("inst-1"))
        self.assertEqual(kk.status("inst-1")["status"], "disconnected")
        self.assertEqual(yt.status("inst-1")["status"], "connected")

    def test_token_isolation_by_provider(self):
        _, _, tokens = _services()
        acc_yt = Account(Provider.YOUTUBE, "UC1", "C")
        tokens.save(acc_yt, TokenPair("at-yt", "rt-yt", 3600, "s"))
        acc_kk = Account(Provider.KICK, "UC1", "C")
        # Misma user_id, distinto provider: sin fuga entre stores.
        self.assertIsNone(tokens.load(acc_kk))

    def test_status_unknown_installation_disconnected(self):
        yt, kk, _ = _services()
        self.assertEqual(yt.status("no-existe")["status"], "disconnected")
        self.assertEqual(kk.status("no-existe")["status"], "disconnected")


if __name__ == "__main__":
    unittest.main()
