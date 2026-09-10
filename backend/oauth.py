"""Orquestación OAuth por proveedor (T-045 YouTube, T-046 Kick): transacciones de un solo uso + conexiones.

Separa OAuth transaction (efímera, con verifier) de backend session (T-044).
El verifier jamás sale del backend: vive en la transacción en memoria y se
consume en el exchange. Callback replay / state replay / transacción expirada
→ REJECT. Sin llamadas al plugin: el plugin sondea status (T-048 lo cablea).
"""
from __future__ import annotations

import base64
import hashlib
import secrets
import threading
import time

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Connection, ConnectionStatus
from backend.ports import OAuthProvider

TRANSACTION_TTL_S = 600  # Decisión T-045: 10 min para completar el consentimiento.
REFRESH_MARGIN_S = 120   # Refrescar si el access expira en <2 min.


def _b64url_sha256(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


class ConnectService:
    """Flujo Connect genérico (un servicio por proveedor). Reloj inyectable."""

    def __init__(self, provider: OAuthProvider, transactions,
                 connections, tokens, clock=None) -> None:
        self._provider = provider
        self._transactions = transactions
        self._connections = connections
        self._tokens = tokens
        self._clock = clock or time.time
        self._refresh_locks: dict[str, threading.Lock] = {}
        self._locks_guard = threading.Lock()

    # --- connect ---
    def start(self, installation_id: str, redirect_uri: str) -> dict:
        verifier = secrets.token_urlsafe(64)[:128]
        transaction = {
            "id": secrets.token_hex(16),
            "provider": self._provider.provider.value,
            "installation_id": installation_id,
            "state": secrets.token_hex(16),
            "code_verifier": verifier,
            "redirect_uri": redirect_uri,
            "created_at": self._clock(),
            "expires_at": self._clock() + TRANSACTION_TTL_S,
            "consumed": False,
        }
        self._transactions.save(transaction)
        client_id = self._provider.client_id_for_url()
        url = self._provider.build_authorization_url(
            client_id, redirect_uri, transaction["state"],
            _b64url_sha256(verifier))
        return {"transaction_id": transaction["id"], "authorization_url": url}

    # --- callback ---
    def callback(self, state: str, code: str, error: str = "") -> dict:
        transaction = self._find_by_state(state)
        if transaction is None:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "bad state")
        if transaction.get("provider") != self._provider.provider.value:
            # Transacción de otro proveedor usada en este callback → REJECT
            # (sin consumir: el callback legítimo sigue siendo posible).
            raise AppError(ErrorCode.PROVIDER_REJECTED, "wrong provider")
        if error:
            self._transactions.consume(transaction["id"])
            # access_denied del usuario → AUTHORIZATION (UI: "Authorization failed").
            raise AppError(ErrorCode.AUTHORIZATION, f"{self._provider.provider.value}:{error}")
        if not code:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "missing code")
        consumed = self._transactions.consume(transaction["id"])
        if consumed is None:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "replay/expired")
        tokens = self._provider.exchange(code, consumed["code_verifier"],
                                         consumed["redirect_uri"])
        account = self._provider.fetch_identity(tokens.access_token)
        self._tokens.save(account, tokens)
        self._connections.save(consumed["installation_id"], self._provider.provider.value, {
            "account": {"provider_user_id": account.provider_user_id,
                        "display_name": account.display_name,
                        "scopes": list(account.scopes)},
            "obtained_at": self._clock(),
        })
        return {"provider": self._provider.provider.value, "status": "connected",
                "account": {"id": account.provider_user_id,
                            "displayName": account.display_name}}

    def _find_by_state(self, state: str) -> dict | None:
        return self._transactions.find_by_state(state)

    # --- status / conexión ---
    def status(self, installation_id: str) -> dict:
        entry = self._connections.load(installation_id, self._provider.provider.value)
        if entry is None:
            return {"provider": self._provider.provider.value, "status": "disconnected"}
        account = entry["account"]
        return {"provider": self._provider.provider.value, "status": "connected",
                "account": {"id": account["provider_user_id"],
                            "displayName": account["display_name"]}}

    def connection(self, installation_id: str) -> Connection | None:
        entry = self._connections.load(installation_id, self._provider.provider.value)
        if entry is None:
            return None
        account = entry["account"]
        return Connection(
            account=Account(provider=self._provider.provider,
                            provider_user_id=account["provider_user_id"],
                            display_name=account["display_name"],
                            scopes=tuple(account["scopes"])),
            status=ConnectionStatus.CONNECTED)

    # --- refresh con single-flight por cuenta ---
    def _lock_for(self, key: str) -> threading.Lock:
        with self._locks_guard:
            return self._refresh_locks.setdefault(key, threading.Lock())

    def ensure_fresh_token(self, installation_id: str) -> str:
        """Access vigente o renovado. Sin loops: un intento de refresh."""
        entry = self._connections.load(installation_id, self._provider.provider.value)
        if entry is None:
            raise AppError(ErrorCode.AUTHENTICATION, "not connected")
        account_data = entry["account"]
        account = Account(provider=self._provider.provider,
                          provider_user_id=account_data["provider_user_id"],
                          display_name=account_data["display_name"],
                          scopes=tuple(account_data["scopes"]))
        tokens = self._tokens.load(account)
        if tokens is None:
            raise AppError(ErrorCode.AUTHENTICATION, "tokens missing")
        age = self._clock() - entry.get("obtained_at", 0)
        if tokens.expires_in and age < tokens.expires_in - REFRESH_MARGIN_S:
            return tokens.access_token
        with self._lock_for(account.provider_user_id):
            # Releer bajo lock: otro hilo pudo refrescar ya.
            entry = self._connections.load(installation_id, self._provider.provider.value)
            tokens = self._tokens.load(account)
            age = self._clock() - entry.get("obtained_at", 0)
            if tokens.expires_in and age < tokens.expires_in - REFRESH_MARGIN_S:
                return tokens.access_token
            if not tokens.refresh_token:
                raise AppError(ErrorCode.SESSION_EXPIRED, "reconnect required")
            try:
                fresh = self._provider.refresh(account, tokens.refresh_token)
            except AppError as exc:
                if exc.code == ErrorCode.PROVIDER_REJECTED:
                    # invalid_grant: refresh revocado/expirado → reconectar.
                    raise AppError(ErrorCode.SESSION_EXPIRED,
                                   "refresh rejected") from exc
                raise
            self._tokens.save(account, fresh)
            entry["obtained_at"] = self._clock()
            self._connections.save(installation_id, self._provider.provider.value, entry)
            return fresh.access_token

    # --- disconnect ---
    def disconnect(self, installation_id: str, revoke_remote: bool = True) -> None:
        """Borrado local siempre; revoke remoto best-effort (F-030)."""
        entry = self._connections.load(installation_id, self._provider.provider.value)
        tokens = None
        if entry is not None:
            account_data = entry["account"]
            tokens = self._tokens.load(Account(
                provider=self._provider.provider,
                provider_user_id=account_data["provider_user_id"],
                display_name=account_data["display_name"],
                scopes=tuple(account_data["scopes"])))
        self._connections.delete(installation_id, self._provider.provider.value)
        if entry is not None:
            self._tokens.delete(Account(
                provider=self._provider.provider,
                provider_user_id=entry["account"]["provider_user_id"],
                display_name=entry["account"]["display_name"],
                scopes=tuple(entry["account"]["scopes"])))
        if revoke_remote and tokens is not None:
            for token in (tokens.access_token, tokens.refresh_token):
                if token:
                    try:
                        self._provider.revoke(token)
                    except AppError:
                        pass  # best-effort: el borrado local ya ocurrió
