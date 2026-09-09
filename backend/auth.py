"""Autenticación plugin↔backend (T-044, ADR-010).

Modelo: bootstrap anónimo rate-limitado → secreto por instalación (vía TLS)
→ sesiones opacas cortas bearer → refresh con firma HMAC + nonce de un uso.

NO hay API keys permanentes ni secretos globales en el plugin. Comprometer una
instalación no revela credencial maestra: cada secreto es por instalación,
revocable y aislado.

Rutas públicas: /health, /ready, /version. Todo lo demás exige sesión (401).
Decisiones T-044: SESSION_TTL_S=1800, NONCE_WINDOW_S=600, CLOCK_SKEW_S=300,
límites §RateLimits. HMAC-SHA256 estándar (stdlib ambos lados); sin keypair
asimétrico (propiedades equivalentes bajo compromiso de endpoint, ver ADR-010).
"""
from __future__ import annotations

import hashlib
import hmac
import secrets
import time
from dataclasses import dataclass

from backend.errors import AppError, ErrorCode
from backend.ports import Installation, InstallationStore, SessionStore

PUBLIC_PATHS = frozenset({"/health", "/ready", "/version"})

SESSION_TTL_S = 1800        # Decisión T-044: sesión opaca de 30 minutos.
NONCE_WINDOW_S = 600        # Nonces válidos 10 min (cubre CLOCK_SKEW + reintentos).
CLOCK_SKEW_S = 300          # Ventana de timestamp ±5 min.

# Límites iniciales T-044 (razonados, ajustables por config en producción):
# bootstrap es anónimo → límites duros por IP y globales; session/refresh/
# revoke van autenticados → límites por instalación (abuso contenido).
BOOTSTRAP_PER_IP_LIMIT = 5
BOOTSTRAP_PER_IP_WINDOW_S = 3600
BOOTSTRAP_GLOBAL_LIMIT = 100
BOOTSTRAP_GLOBAL_WINDOW_S = 3600
AUTH_PER_INSTALL_LIMIT = 30
AUTH_PER_INSTALL_WINDOW_S = 60
AUTH_PER_IP_LIMIT = 200
AUTH_PER_IP_WINDOW_S = 60


def is_public(path: str) -> bool:
    return path in PUBLIC_PATHS


def extract_bearer(authorization: str) -> str | None:
    scheme, _, value = authorization.partition(" ")
    if scheme.lower() != "bearer" or not value.strip():
        return None
    return value.strip()


def sign_installation_secret(secret: str, installation_id: str,
                             timestamp: int, nonce: str) -> str:
    """Firma HMAC-SHA256 del cliente. Mensaje canónico con separadores fijos."""
    msg = f"{installation_id}|{timestamp}|{nonce}".encode("utf-8")
    return hmac.new(secret.encode("utf-8"), msg, hashlib.sha256).hexdigest()


@dataclass
class SessionRecord:
    token: str
    installation_id: str
    issued_at: float
    expires_at: float
    revoked: bool = False


class AuthService:
    """Emisión/validación de sesiones. Reloj inyectable para tests."""

    def __init__(self, installations: InstallationStore, sessions: SessionStore,
                 clock=None) -> None:
        self._installations = installations
        self._sessions = sessions
        self._clock = clock or time.time
        self._nonces: dict[str, float] = {}  # nonce -> expiry (un solo uso)

    # --- instalación ---
    def bootstrap(self) -> dict:
        installation_id = secrets.token_hex(16)
        secret = secrets.token_urlsafe(32)
        now = self._clock()
        self._installations.create(
            Installation(id=installation_id,
                         created_at=_iso(now)), secret)
        # El secreto se entrega UNA vez (vía TLS); el backend no lo reemite.
        return {"installation_id": installation_id, "installation_secret": secret}

    # --- firma / nonce ---
    def _check_signature(self, installation_id: str, timestamp: int,
                         nonce: str, signature: str) -> None:
        now = self._clock()
        if abs(now - timestamp) > CLOCK_SKEW_S + NONCE_WINDOW_S:
            raise AppError(ErrorCode.AUTHENTICATION, "stale timestamp")
        found = self._installations.load(installation_id)
        if found is None:
            raise AppError(ErrorCode.AUTHENTICATION, "unknown installation")
        installation, secret = found
        if installation.revoked:
            raise AppError(ErrorCode.AUTHENTICATION, "installation revoked")
        self._use_nonce(nonce, now)
        expected = sign_installation_secret(secret, installation_id, timestamp, nonce)
        if not hmac.compare_digest(expected, signature):
            raise AppError(ErrorCode.AUTHENTICATION, "bad signature")

    def _use_nonce(self, nonce: str, now: float) -> None:
        if not nonce or len(nonce) < 16:
            raise AppError(ErrorCode.AUTHENTICATION, "bad nonce")
        expiry = self._nonces.get(nonce)
        if expiry is not None and expiry > now:
            raise AppError(ErrorCode.AUTHENTICATION, "nonce reuse")
        # Poda oportunista + registro de un solo uso.
        self._nonces = {n: e for n, e in self._nonces.items() if e > now}
        self._nonces[nonce] = now + NONCE_WINDOW_S

    # --- sesiones ---
    def create_session(self, installation_id: str, timestamp: int,
                       nonce: str, signature: str) -> dict:
        self._check_signature(installation_id, timestamp, nonce, signature)
        return self._issue(installation_id)

    def _issue(self, installation_id: str) -> dict:
        now = self._clock()
        token = secrets.token_urlsafe(32)
        record = SessionRecord(token=token, installation_id=installation_id,
                               issued_at=now, expires_at=now + SESSION_TTL_S)
        self._sessions.save_session(token, _record_to_dict(record))
        return {"session_token": token, "expires_in": SESSION_TTL_S}

    def validate_session(self, token: str) -> SessionRecord:
        payload = self._sessions.load_session(token) if token else None
        if payload is None:
            raise AppError(ErrorCode.AUTHENTICATION, "no valid session")
        record = _record_from_dict(payload)
        if record.revoked:
            raise AppError(ErrorCode.AUTHENTICATION, "session revoked")
        found = self._installations.load(record.installation_id)
        if found is None or found[0].revoked:
            raise AppError(ErrorCode.AUTHENTICATION, "installation revoked")
        if self._clock() >= record.expires_at:
            raise AppError(ErrorCode.SESSION_EXPIRED, "session expired")
        return record

    def refresh_session(self, token: str, installation_id: str, timestamp: int,
                        nonce: str, signature: str) -> dict:
        """Rotación: verifica firma fresca + sesión vigente, emite nueva e
        invalida la anterior. Carrera concurrente: gana la primera; la segunda
        recibe 401 y el cliente re-autentica vía /auth/session."""
        self._check_signature(installation_id, timestamp, nonce, signature)
        record = self.validate_session(token)
        if record.installation_id != installation_id:
            raise AppError(ErrorCode.AUTHENTICATION, "session mismatch")
        record.revoked = True
        self._sessions.save_session(token, _record_to_dict(record))
        return self._issue(installation_id)

    def revoke_session(self, token: str) -> None:
        payload = self._sessions.load_session(token) if token else None
        if payload is None:
            return  # idempotente: revocar dos veces es éxito
        record = _record_from_dict(payload)
        record.revoked = True
        self._sessions.save_session(token, _record_to_dict(record))

    def revoke_installation(self, installation_id: str, timestamp: int,
                            nonce: str, signature: str) -> None:
        """Revoca la instalación y todas sus sesiones conocidas vía firma."""
        self._check_signature(installation_id, timestamp, nonce, signature)
        self._installations.revoke(installation_id)


def _iso(ts: float) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))


def _record_to_dict(record: SessionRecord) -> dict:
    return {"token": record.token, "installation_id": record.installation_id,
            "issued_at": record.issued_at, "expires_at": record.expires_at,
            "revoked": record.revoked}


def _record_from_dict(payload: dict) -> SessionRecord:
    return SessionRecord(token=payload["token"],
                         installation_id=payload["installation_id"],
                         issued_at=payload["issued_at"],
                         expires_at=payload["expires_at"],
                         revoked=payload.get("revoked", False))
