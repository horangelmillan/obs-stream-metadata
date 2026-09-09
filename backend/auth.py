"""Frontera de autenticación plugin↔backend (T-043 prepara, T-044 implementa).

NO hay API keys permanentes ni secretos hardcodeados. Rutas públicas limitadas
a liveness/readiness/version. Todo lo demás exige sesión (401 sin ella) y el
mecanismo definitivo de emisión/validación vive en T-044.
"""
from __future__ import annotations

PUBLIC_PATHS = frozenset({"/health", "/ready", "/version"})


def is_public(path: str) -> bool:
    return path in PUBLIC_PATHS


def extract_bearer(authorization: str) -> str | None:
    scheme, _, value = authorization.partition(" ")
    if scheme.lower() != "bearer" or not value.strip():
        return None
    return value.strip()
