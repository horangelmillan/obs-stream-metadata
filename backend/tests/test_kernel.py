"""Tests Shared Kernel: capacidades, inmutabilidad y aislamiento de proveedor."""
import pathlib
import unittest

from backend.kernel import (CAPABILITIES, Account, Connection, ConnectionStatus,
                            MetadataUpdate, Provider)


class KernelTest(unittest.TestCase):
    def test_description_only_youtube(self):
        self.assertTrue(CAPABILITIES[Provider.YOUTUBE].stream_description)
        self.assertFalse(CAPABILITIES[Provider.TWITCH].stream_description)
        self.assertFalse(CAPABILITIES[Provider.KICK].stream_description)

    def test_all_providers_support_title(self):
        for provider in Provider:
            self.assertTrue(CAPABILITIES[provider].title, provider)

    def test_dataclasses_frozen(self):
        acc = Account(Provider.KICK, "u1", "Name")
        with self.assertRaises(Exception):
            acc.display_name = "Other"  # type: ignore[misc]

    def test_metadata_update_defaults(self):
        cmd = MetadataUpdate(platforms=(Provider.YOUTUBE,), title="T")
        self.assertEqual(cmd.description, "")
        self.assertEqual(cmd.broadcast_id, "")

    def test_connection_default_status(self):
        acc = Account(Provider.TWITCH, "u1", "Name")
        self.assertEqual(Connection(acc).status, ConnectionStatus.CONNECTED)

    def test_kernel_has_no_provider_details(self):
        """El kernel no debe nombrar endpoints, secretos ni quirks de proveedor."""
        src = pathlib.Path(__file__).resolve().parent.parent.joinpath("kernel.py")
        text = src.read_text(encoding="utf-8").lower()
        for forbidden in ("http", "secret", "token_url", "authorize",
                          "localhost", "127.0.0.1", "pkce", "client_"):
            self.assertNotIn(forbidden, text, forbidden)


if __name__ == "__main__":
    unittest.main()
