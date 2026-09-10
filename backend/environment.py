"""Entorno explícito DEV/PROD (T-053).

`ConnectionMode != environment` (ADR-012): esto NO es un modo de conexión,
es la identidad del despliegue backend con el que se habla.

Reglas:
- El entorno es explícito: solo "development" o "production".
  Cualquier otro valor falla rápido (sin adivinanzas, sin fallback).
- "development" conserva el flujo local actual sin restricciones nuevas.
- "production" exige piezas productivas: rechaza stores marcados
  DEVELOPMENT_ONLY y exige `public_base_url` HTTPS. Sin autocorrección:
  un mismatch es un error de configuración, no algo que "arreglar".
- Los secretos nunca se renombran aquí: el binding secreto↔entorno lo da
  el store elegido explícitamente por despliegue (EnvSecretStore es
  DEVELOPMENT_ONLY y por tanto inutilizable en producción).
"""
from __future__ import annotations


DEVELOPMENT = "development"
PRODUCTION = "production"

KNOWN_ENVIRONMENTS = (DEVELOPMENT, PRODUCTION)


class EnvironmentError(ValueError):
    """Configuración de entorno inválida o insegura (fail-fast)."""


def normalize(raw: str | None, *, default: str = DEVELOPMENT) -> str:
    """Normaliza el nombre de entorno o falla rápido.

    `None`/vacío → default (dirección segura: development).
    Desconocido → EnvironmentError. Sin fallback entre entornos.
    """
    if raw is None or not str(raw).strip():
        return default
    value = str(raw).strip().lower()
    if value not in KNOWN_ENVIRONMENTS:
        raise EnvironmentError(
            f"unknown environment {raw!r}: expected one of {KNOWN_ENVIRONMENTS}")
    return value


def assert_production_ready(*, env: str, public_base_url: str,
                            stores: dict[str, object],
                            limiter: object = None) -> None:
    """Gates de producción (T-053/T-054). No-op en development.

    - Rechaza stores DEVELOPMENT_ONLY (secretos/sesiones/conexiones de
      grado-dev no pueden custodiar identidad productiva).
    - Rechaza `public_base_url` no-HTTPS (los redirects OAuth de
      producción no pueden construirse sobre HTTP).
    - Rechaza limiter permisivo total (T-054: AllowAllRateLimiter no
      puede ser la puerta global productiva; el despliegue fija límites
      explícitos).
    """
    if env != PRODUCTION:
        return
    url = (public_base_url or "").strip().lower()
    if not url.startswith("https://"):
        raise EnvironmentError(
            "production requires an https public_base_url "
            f"(got {public_base_url!r})")
    for role, store in stores.items():
        if getattr(store, "DEVELOPMENT_ONLY", False):
            raise EnvironmentError(
                f"production refuses DEVELOPMENT_ONLY store for {role}: "
                f"{type(store).__name__}")
    if limiter is not None and getattr(limiter, "ALLOW_ALL", False):
        raise EnvironmentError(
            "production refuses allow-all global limiter: "
            "configure explicit limits")
