"""Modelo de errores (ADR-009, T-043 §16).

Taxonomía alineada con AGENTS.md §28. Respuestas HTTP seguras:
{"error": {"code", "message", "requestId"}} — nunca secretos, tokens,
authorization codes, verifiers, trazas internas ni credenciales.
"""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class ErrorCode(str, Enum):
    AUTHENTICATION = "authentication_error"      # 401: sin sesión / sesión inválida
    AUTHORIZATION = "authorization_error"        # 403: sin permiso / scope insuficiente
    INVALID_REQUEST = "invalid_request"          # 400: validación de entrada
    SESSION_EXPIRED = "session_expired"          # 401: sesión caducada → reconectar
    PROVIDER_REJECTED = "provider_rejected"      # 502: el proveedor rechazó (400/403/404/409 origen)
    PROVIDER_RATE_LIMITED = "provider_rate_limited"  # 429: sin reintento agresivo
    RATE_LIMITED = "rate_limited"                    # 429: backend propio
    PROVIDER_UNAVAILABLE = "provider_unavailable"    # 502: 5xx origen / red
    INTERNAL = "internal_error"                  # 500: fallo propio, sin detalle


_HTTP_STATUS: dict[ErrorCode, int] = {
    ErrorCode.AUTHENTICATION: 401,
    ErrorCode.AUTHORIZATION: 403,
    ErrorCode.INVALID_REQUEST: 400,
    ErrorCode.SESSION_EXPIRED: 401,
    ErrorCode.PROVIDER_REJECTED: 502,
    ErrorCode.PROVIDER_RATE_LIMITED: 429,
    ErrorCode.RATE_LIMITED: 429,
    ErrorCode.PROVIDER_UNAVAILABLE: 502,
    ErrorCode.INTERNAL: 500,
}

# Mensajes seguros para mostrar al usuario/plugin (sin detalle interno).
_SAFE_MESSAGE: dict[ErrorCode, str] = {
    ErrorCode.AUTHENTICATION: "Authentication required.",
    ErrorCode.AUTHORIZATION: "Insufficient permissions.",
    ErrorCode.INVALID_REQUEST: "Invalid request.",
    ErrorCode.SESSION_EXPIRED: "Session expired. Reconnect the account.",
    ErrorCode.PROVIDER_REJECTED: "The provider rejected the request.",
    ErrorCode.PROVIDER_RATE_LIMITED: "Provider rate limit reached. Retry later.",
    ErrorCode.RATE_LIMITED: "Rate limit reached. Retry later.",
    ErrorCode.PROVIDER_UNAVAILABLE: "Provider temporarily unavailable.",
    ErrorCode.INTERNAL: "Internal error.",
}


@dataclass(frozen=True)
class AppError(Exception):
    code: ErrorCode
    detail: str = ""  # detalle interno: SOLO para logs, jamás en respuestas

    def http_status(self) -> int:
        return _HTTP_STATUS[self.code]

    def safe_message(self) -> str:
        return _SAFE_MESSAGE[self.code]

    def public_body(self, request_id: str) -> dict:
        return {"error": {"code": self.code.value, "message": self.safe_message(),
                          "requestId": request_id}}
