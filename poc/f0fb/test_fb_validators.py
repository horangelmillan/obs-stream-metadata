#!/usr/bin/env python3
"""FB-1 Step1 RED: validadores offline Facebook (T-070, estilo T-030 28/28).

Cero codigo de producto: solo PoC offline en poc/f0fb/.
Fuentes: docs/T0FB-RESEARCH.md D1-D14 + Graph API v25/v26 (2026-09-20).
Reglas:
- Titulo 1-254 (D2, referencia user/live_videos v25.0 "Maximum 254").
- Descripcion SI existe (a diferencia de Twitch/Kick); sin limite
  inventado: el validador solo exige tipo str (brief no fija max).
- Jamas publish_to_groups/email (brief §9: Groups API deprecada v19).
- UPDATE = POST /{live-video-id}; nunca Video API en vivo
  (LIVE_VIDEO__EDIT_API_NOT_ALLOWED).
- privacy "Pueblico" = {"value": "EVERYONE"} (D11, hipotesis update
  post-create a validar en sonda viva).
- Tokens: sin refresh clasico; 190 -> SESSION_EXPIRED (D8).
- Elegibilidad 1363120/1363144 -> AUTHORIZATION (brief §5).

Uso: python -m unittest discover -s poc/f0fb -v
"""

import sys
import os
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from fb_validators import (
    fb_title_ok,
    fb_description_ok,
    facebook_auth_url,
    fb_update_payload,
    fb_privacy_payload,
    fb_status_payload,
    fb_end_live_payload,
    classify_fb,
    fb_required_scopes,
)


class FbTitleTest(unittest.TestCase):
    def test_empty_rejected(self):
        ok, _ = fb_title_ok("")
        self.assertFalse(ok)

    def test_min1_ok(self):
        ok, _ = fb_title_ok("A")
        self.assertTrue(ok)

    def test_max254_ok(self):
        ok, _ = fb_title_ok("x" * 254)
        self.assertTrue(ok)

    def test_255_rejected(self):
        ok, _ = fb_title_ok("x" * 255)
        self.assertFalse(ok)


class FbDescriptionTest(unittest.TestCase):
    def test_empty_ok(self):
        ok, _ = fb_description_ok("")
        self.assertTrue(ok)

    def test_long_ok_no_invented_limit(self):
        # El brief no fija max para descripcion FB: no inventar limite.
        ok, _ = fb_description_ok("y" * 6000)
        self.assertTrue(ok)

    def test_none_rejected(self):
        ok, _ = fb_description_ok(None)
        self.assertFalse(ok)


class FbAuthUrlTest(unittest.TestCase):
    def test_contains_dialog_and_params(self):
        u = facebook_auth_url("APP123", "http://localhost:9009/cb", "ST aleatorio".replace(" ", ""))
        self.assertIn("facebook.com/v26.0/dialog/oauth", u)
        self.assertIn("client_id=APP123", u)
        self.assertIn("state=", u)

    def test_no_secret_in_url(self):
        u = facebook_auth_url("APP123", "http://localhost:9009/cb", "ST1")
        self.assertNotIn("client_secret", u)
        self.assertNotIn("access_token", u)

    def test_uses_login_success_or_localhost_redirect(self):
        u = facebook_auth_url("APP123", "http://localhost:9009/cb", "ST1")
        self.assertTrue("localhost" in u or "login_success" in u)


class FbPayloadTest(unittest.TestCase):
    def test_update_title_only_minimal(self):
        p = fb_update_payload(title="Hola", description="")
        self.assertEqual(p.get("title"), "Hola")
        self.assertNotIn("channel_description", p)
        self.assertNotIn("stream_title", p)

    def test_update_title_desc(self):
        p = fb_update_payload(title="T", description="D")
        self.assertEqual((p["title"], p["description"]), ("T", "D"))

    def test_privacy_everyone(self):
        p = fb_privacy_payload(public=True)
        self.assertEqual(p, {"privacy": {"value": "EVERYONE"}})

    def test_status_live_now(self):
        p = fb_status_payload(go_live=True)
        self.assertEqual(p.get("status"), "LIVE_NOW")

    def test_end_live(self):
        p = fb_end_live_payload()
        self.assertTrue(p.get("end_live_video") is True)

    def test_no_video_api_fields(self):
        # Nunca confundir con campos de Video API / canal ajeno.
        p = fb_update_payload(title="T", description="D")
        for bad in ("channel_description", "stream_title", "snippet"):
            self.assertNotIn(bad, p)


class FbClassifyTest(unittest.TestCase):
    def test_200_ok(self):
        self.assertEqual(classify_fb(200, {}), "Success")

    def test_400_invalid(self):
        self.assertEqual(classify_fb(400, {}), "INVALID_REQUEST")

    def test_190_session_expired(self):
        self.assertEqual(classify_fb(400, {"code": 190}), "SESSION_EXPIRED")

    def test_eligibility_60d(self):
        self.assertEqual(classify_fb(200, {"code": 1363120}), "AUTHORIZATION")

    def test_eligibility_100followers(self):
        self.assertEqual(classify_fb(200, {"code": 1363144}), "AUTHORIZATION")

    def test_10_review_required(self):
        self.assertEqual(classify_fb(403, {"code": 10}), "AUTHORIZATION")

    def test_429_rate_limited(self):
        self.assertEqual(classify_fb(429, {}), "PROVIDER_RATE_LIMITED")

    def test_5xx_unavailable(self):
        self.assertEqual(classify_fb(500, {}), "PROVIDER_UNAVAILABLE")


class FbScopesTest(unittest.TestCase):
    def test_profile_min_publish_video(self):
        s = fb_required_scopes("profile")
        self.assertIn("publish_video", s)

    def test_page_set(self):
        s = fb_required_scopes("page")
        self.assertIn("pages_manage_posts", s)
        self.assertIn("pages_read_engagement", s)

    def test_never_groups(self):
        for mode in ("profile", "page"):
            self.assertNotIn("publish_to_groups", fb_required_scopes(mode))

    def test_never_email(self):
        for mode in ("profile", "page"):
            self.assertNotIn("email", fb_required_scopes(mode))


if __name__ == "__main__":
    unittest.main()
