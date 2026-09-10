"""Tests T-053: entorno explícito DEV/PROD + fail-fast (sin fallback).

Garantías:
- `development`/`production` son los únicos entornos (inválido = error).
- Producción rechaza stores DEVELOPMENT_ONLY y public_base_url no-HTTPS.
- `/version` expone `env` (operador distingue el entorno).
- Ante mismatch no hay fallback silencioso: se lanza, no se corrige.
- Solo fakes sintéticos; ningún secreto real (ver test_leak_guard).
"""
import unittest

from backend.app import create_app
from backend.config import Settings, load_settings
from backend.environment import (DEVELOPMENT, PRODUCTION, EnvironmentError,
                                 assert_production_ready, normalize)
from backend.stores import (AllowAllRateLimiter, EnvSecretStore,
                            FixedWindowRateLimiter, InMemorySessionStore)


class _ProdSecrets:
    """Doble productivo: sin DEVELOPMENT_ONLY, valores sintéticos."""

    def get(self, name):
        return "fake-prod-" + name


class _ProdSessions:
    def save_session(self, sid, payload):
        pass

    def load_session(self, sid):
        return None

    def delete_session(self, sid):
        pass


class _ProdInstallations:
    def create(self, installation, secret):
        pass

    def load(self, installation_id):
        return None

    def revoke(self, installation_id):
        pass


def _prod_settings(**over):
    kw = dict(host="127.0.0.1", port=0, env=PRODUCTION,
              public_base_url="https://backend.example.com")
    kw.update(over)
    return Settings(**kw)


def _prod_wiring(**over):
    kw = dict(secrets=_ProdSecrets(), sessions=_ProdSessions(),
              installations=_ProdInstallations(), providers={},
              limiter=FixedWindowRateLimiter(600, 60))
    kw.update(over)
    return kw


class EnvNormalizeTest(unittest.TestCase):
    def test_known_environments(self):
        self.assertEqual(normalize("development"), DEVELOPMENT)
        self.assertEqual(normalize("production"), PRODUCTION)
        self.assertEqual(normalize("Production"), PRODUCTION)

    def test_missing_means_development(self):
        self.assertEqual(normalize(None), DEVELOPMENT)
        self.assertEqual(normalize(""), DEVELOPMENT)

    def test_unknown_fails_fast(self):
        for bad in ("staging", "prod", "dev", "PROD2", " development "):
            if bad.strip().lower() in ("development", "production"):
                continue
            with self.assertRaises(EnvironmentError, msg=bad):
                normalize(bad)


class LoadSettingsEnvTest(unittest.TestCase):
    def test_default_is_development(self):
        self.assertEqual(load_settings({}).env, DEVELOPMENT)

    def test_production_accepted(self):
        env = {"STREAM_META_BACKEND_ENV": "production"}
        self.assertEqual(load_settings(env).env, PRODUCTION)

    def test_unknown_rejected(self):
        with self.assertRaises(EnvironmentError):
            load_settings({"STREAM_META_BACKEND_ENV": "staging"})


class BindHostTest(unittest.TestCase):
    """T-056: bind por entorno (Cloud Run necesita 0.0.0.0 en prod)."""

    def test_dev_defaults_loopback(self):
        settings = load_settings({})
        self.assertEqual(settings.env, DEVELOPMENT)
        self.assertEqual(settings.host, "127.0.0.1")

    def test_prod_defaults_all_interfaces(self):
        settings = load_settings({"STREAM_META_BACKEND_ENV": "production"})
        self.assertEqual(settings.host, "0.0.0.0")

    def test_explicit_host_wins_in_prod(self):
        settings = load_settings({"STREAM_META_BACKEND_ENV": "production",
                                  "STREAM_META_BACKEND_HOST": "custom-host"})
        self.assertEqual(settings.host, "custom-host")

    def test_explicit_host_wins_in_dev(self):
        settings = load_settings({"STREAM_META_BACKEND_HOST": "0.0.0.0"})
        self.assertEqual(settings.host, "0.0.0.0")

    def test_port_contract_unchanged(self):
        base = {"STREAM_META_BACKEND_ENV": "production"}
        self.assertEqual(load_settings(base).port, 8080)
        self.assertEqual(
            load_settings(dict(base, PORT="9090")).port, 9090)
        self.assertEqual(
            load_settings(dict(base, PORT="9090",
                               STREAM_META_BACKEND_PORT="9091")).port, 9091)


class ProductionGatesTest(unittest.TestCase):
    def test_dev_keeps_current_flow(self):
        app = create_app(settings=Settings(host="127.0.0.1", port=0),
                         providers={})
        self.assertEqual(app.settings.env, DEVELOPMENT)

    def test_prod_rejects_dev_secret_store(self):
        with self.assertRaises(EnvironmentError):
            create_app(settings=_prod_settings(),
                       secrets=EnvSecretStore(),
                       sessions=_ProdSessions(),
                       installations=_ProdInstallations(), providers={})

    def test_prod_rejects_dev_session_store(self):
        with self.assertRaises(EnvironmentError):
            create_app(settings=_prod_settings(), secrets=_ProdSecrets(),
                       sessions=InMemorySessionStore(),
                       installations=_ProdInstallations(), providers={})

    def test_prod_rejects_http_base_url(self):
        with self.assertRaises(EnvironmentError):
            create_app(settings=_prod_settings(
                           public_base_url="http://127.0.0.1:8080"),
                       **_prod_wiring())

    def test_prod_rejects_allow_all_limiter(self):
        kw = _prod_wiring(limiter=AllowAllRateLimiter())
        with self.assertRaises(EnvironmentError):
            create_app(settings=_prod_settings(), **kw)

    def test_dev_keeps_allow_all_limiter(self):
        app = create_app(settings=Settings(host="127.0.0.1", port=0),
                         providers={},
                         limiter=AllowAllRateLimiter())
        self.assertEqual(app.settings.env, DEVELOPMENT)

    def test_prod_accepts_https_with_prod_stores(self):
        app = create_app(settings=_prod_settings(), **_prod_wiring())
        self.assertEqual(app.settings.env, PRODUCTION)

    def test_no_fallback_on_prod_failure(self):
        settings = _prod_settings(public_base_url="http://127.0.0.1:8080")
        with self.assertRaises(EnvironmentError):
            create_app(settings=settings, **_prod_wiring())
        # El fallo no muta ni degrada la configuración a development.
        self.assertEqual(settings.env, PRODUCTION)

    def test_dev_store_still_flagged(self):
        self.assertTrue(EnvSecretStore.DEVELOPMENT_ONLY)
        self.assertTrue(InMemorySessionStore.DEVELOPMENT_ONLY)


class VersionEnvTest(unittest.TestCase):
    def test_version_reports_development(self):
        app = create_app(settings=Settings(host="127.0.0.1", port=0),
                         providers={})
        self.assertEqual(app.version()["env"], DEVELOPMENT)

    def test_version_reports_production(self):
        app = create_app(settings=_prod_settings(), **_prod_wiring())
        self.assertEqual(app.version()["env"], PRODUCTION)
        self.assertNotIn("secret", str(app.version()).lower())


if __name__ == "__main__":
    unittest.main()
