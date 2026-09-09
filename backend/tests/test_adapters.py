"""Tests adapters: metadatos declarativos + aislamiento (sin red)."""
import unittest

from backend.adapters.kick import KickProvider
from backend.adapters.twitch import TwitchProvider
from backend.adapters.youtube import YouTubeProvider
from backend.errors import AppError, ErrorCode
from backend.kernel import OAuthSession, Provider


def _session(provider: Provider) -> OAuthSession:
    return OAuthSession(id="s", provider=provider, state="st")


class AdaptersTest(unittest.TestCase):
    def test_scopes_minimal(self):
        self.assertEqual(YouTubeProvider().SCOPES,
                         ("https://www.googleapis.com/auth/youtube.force-ssl",))
        self.assertIn("channel:write", KickProvider().SCOPES)
        self.assertEqual(TwitchProvider().SCOPES, ("channel:manage:broadcast",))

    def test_capabilities_match_kernel_matrix(self):
        self.assertTrue(YouTubeProvider().capability.stream_description)
        self.assertFalse(KickProvider().capability.stream_description)
        self.assertFalse(TwitchProvider().capability.stream_description)

    def test_not_implemented_raises_internal_without_leak(self):
        for cls in (YouTubeProvider, KickProvider, TwitchProvider):
            with self.assertRaises(AppError) as ctx:
                cls().exchange_code(_session(cls().provider), "code")
            self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)

    def test_twitch_stays_direct(self):
        self.assertTrue(TwitchProvider.DIRECT_FROM_PLUGIN)
        self.assertFalse(getattr(YouTubeProvider, "DIRECT_FROM_PLUGIN", False))
        self.assertFalse(getattr(KickProvider, "DIRECT_FROM_PLUGIN", False))


if __name__ == "__main__":
    unittest.main()
