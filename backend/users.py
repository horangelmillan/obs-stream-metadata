"""Cuentas de usuario Managed — Fase 1 (spec 2026-10-26, Enfoque A).

Contiene:
- modelo `User` (username unico, hash PBKDF2-SHA256 con salt 16B y
  600_000 iteraciones; sin email en v1, sin verificacion ni reset);
- `UserExistsError` (equivalente a nivel store del 409 de Fase 2);
- `adopt_installation`: asigna `user_id`/`owner_user_id` a la instalacion
  y sus filas sin regenerar backendInstall ni borrar conexiones/tokens
  (idempotente; instalacion desconocida = conteos a cero, sin efectos).

Este modulo no loguea nada: ni contrasenas, ni hashes, ni tokens. La
redaccion en logs la cubre `RedactingFilter` + `redact_mapping` (la clave
`password_hash` ya cae bajo la regla `password`).
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import re
import secrets
import time
from dataclasses import dataclass

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, Provider

PASSWORD_ALGORITHM = "pbkdf2-sha256"
PASSWORD_ITERATIONS = 600_000
SALT_BYTES = 16

# Fase 2 (spec §11 [P] fijado en TDD): decisiones de validación y límites.
# - username: strip, 3–32 chars, [A-Za-z0-9._-], único exacto
#   (case-sensitive; sin normalización de caso para no colisionar).
# - password: 12–128 chars (mínimo 12; sin reglas de complejidad
#   inventadas: la entropía la da PBKDF2 600k + salt 16B).
# - login rate-limit: 10/10min por IP + 5/10min por username.
# - signup rate-limit: 10/10min por IP (anti-abuso, mismo orden).
# - sesión de usuario: opaca, 15 días deslizantes con tope absoluto de
#   30 días desde el login inicial (decisión [D] 2026-10-26): cada
#   actividad válida renueva +15 días sin pasar el tope; superado el
#   tope o 15 días sin actividad → login de nuevo.
MIN_PASSWORD_LEN = 12
MAX_PASSWORD_LEN = 128
USERNAME_MIN_LEN = 3
USERNAME_MAX_LEN = 32
_USERNAME_RE = re.compile(r"[A-Za-z0-9._-]+")
USER_SESSION_TTL_S = 15 * 24 * 3600  # 1296000: ventana deslizante
USER_SESSION_MAX_TTL_S = 30 * 24 * 3600  # 2592000: tope absoluto login
LOGIN_PER_IP_LIMIT = 10
LOGIN_PER_IP_WINDOW_S = 600
LOGIN_PER_USER_LIMIT = 5
LOGIN_PER_USER_WINDOW_S = 600
SIGNUP_PER_IP_LIMIT = 10
SIGNUP_PER_IP_WINDOW_S = 600


class UserExistsError(Exception):
    """Username ya registrado (Fase 2 lo traduce a 409)."""


@dataclass(frozen=True)
class User:
    """Cuenta Managed: identidad de persona del backend (spec §3/§6)."""

    id: str
    username: str
    password_hash: str
    created_at: int = 0  # epoch UTC (BIGINT en DB)
    updated_at: int = 0  # epoch UTC (BIGINT en DB)
    # Reservadas v1 (spec §3/§10): NULL sin uso ni codigo que las active.
    totp_secret: str | None = None
    oauth_provider: str | None = None
    oauth_subject: str | None = None


def new_user_id() -> str:
    """Identificador opaco (misma entropia que `bootstrap`: 128 bits)."""
    return secrets.token_hex(16)


def utc_now() -> int:
    return int(time.time())


def hash_password(plain: str) -> str:
    """`pbkdf2-sha256$iter$salt_b64$hash_b64` (salt 16B aleatorio)."""
    if not plain:
        raise ValueError("empty password")
    salt = secrets.token_bytes(SALT_BYTES)
    digest = hashlib.pbkdf2_hmac("sha256", plain.encode("utf-8"), salt,
                                 PASSWORD_ITERATIONS)
    return (f"{PASSWORD_ALGORITHM}${PASSWORD_ITERATIONS}"
            f"${base64.b64encode(salt).decode()}"
            f"${base64.b64encode(digest).decode()}")


def verify_password(plain: str, stored: str) -> bool:
    """True si coincide; False ante cualquier formato/valor inesperado."""
    try:
        algo, iters, salt_b, hash_b = str(stored).split("$")
        if algo != PASSWORD_ALGORITHM or int(iters) <= 0:
            return False
        expected = base64.b64decode(hash_b)
        digest = hashlib.pbkdf2_hmac("sha256", plain.encode("utf-8"),
                                     base64.b64decode(salt_b), int(iters))
    except Exception:
        return False
    return hmac.compare_digest(digest, expected)


# Hash fijo para logins de usuarios inexistentes: el 401 normalizado
# (sin distinguir existe/no-existe) cuesta un PBKDF2 también en ese
# caso, sin fuga por temporización gruesa. Se calcula una vez.
_DUMMY_HASH = hash_password("dummy-user-never-exists-00")


def validate_username(raw) -> str:
    """Normaliza (strip) y valida; AppError 400 ante cualquier forma rara."""
    if not isinstance(raw, str):
        raise AppError(ErrorCode.INVALID_REQUEST, "bad username")
    username = raw.strip()
    if not (USERNAME_MIN_LEN <= len(username) <= USERNAME_MAX_LEN):
        raise AppError(ErrorCode.INVALID_REQUEST, "bad username")
    if _USERNAME_RE.fullmatch(username) is None:
        raise AppError(ErrorCode.INVALID_REQUEST, "bad username")
    return username


def validate_password(raw) -> str:
    """Longitud 12–128; AppError 400 si no. Sin complejidad inventada."""
    if not isinstance(raw, str):
        raise AppError(ErrorCode.INVALID_REQUEST, "bad password")
    if not (MIN_PASSWORD_LEN <= len(raw) <= MAX_PASSWORD_LEN):
        raise AppError(ErrorCode.INVALID_REQUEST, "bad password")
    return raw


def check_login(password: str, user: User | None) -> User | None:
    """Verifica credenciales con 401 normalizado (sin distinguir existe).

    Devuelve el `User` si coinciden, None si no. Ante usuario
    inexistente verifica contra `_DUMMY_HASH` para igualar el coste.
    Jamás loguea nada (este módulo no loguea).
    """
    if user is None:
        verify_password(password if isinstance(password, str) else "",
                        _DUMMY_HASH)
        return None
    if not isinstance(password, str):
        return None
    return user if verify_password(password, user.password_hash) else None


def adopt_installation(user_id: str, installation_id: str, *,
                       installations, sessions, connections,
                       tokens) -> dict:
    """Adopta una instalacion anonima para un usuario (spec §3).

    Asigna `user_id` (installations/sessions/connections) y
    `owner_user_id` (tokens de las cuentas conectadas a esa instalacion)
    sin regenerar el secreto de backendInstall y sin borrar nada.
    Idempotente: repetirla devuelve los mismos conteos y deja el mismo
    estado. Instalacion desconocida: conteos a cero, sin efectos.
    """
    if not user_id or not installation_id:
        raise ValueError("adopt requires user_id and installation_id")
    counts = {"installations": 0, "sessions": 0, "connections": 0,
              "tokens": 0}
    if not installations.set_user_id(installation_id, user_id):
        return counts
    counts["installations"] = 1
    counts["sessions"] = sessions.set_user_id_for_installation(
        installation_id, user_id)
    pairs = connections.set_user_id_for_installation(installation_id,
                                                     user_id)
    counts["connections"] = len(pairs)
    for provider, provider_user_id in pairs:
        if tokens.set_owner(provider, provider_user_id, user_id):
            counts["tokens"] += 1
    return counts


def erase_user(user_id: str, *, services, sessions, transactions,
               installations, connections, tokens, users,
               user_sessions=None) -> dict:
    """Borrado en cascada por USUARIO (Fase 5a, spec §5).

    Contrato HTTP `POST /users/erase` (para activar el placeholder de
    Fase 4 en 5b): requiere auth doble Managed ya verificada por el
    llamante (instalación + usuario, igual que Fase 3); request sin
    cuerpo relevante; response 200::

        {"erased": {"users": 0|1, "installations": n, "sessions": n,
                    "connections": n, "tokens": n, "transactions": n},
         "revoked": {provider: bool}}

    - `sessions` agrega sesiones de instalación y de usuario (mismo
      store por defecto; si `user_sessions` es otro objeto se suma).
    - Solo conteos y bools: jamás tokens, secretos, hashes ni IDs.
    - Orden F-030: DELETEs locales primero (tokens copiados en
      memoria), revoke remoto best-effort al final; un fallo de red
      jamás resucita estado. Idempotente: usuario desconocido o ya
      borrado → todo a cero y `revoked` vacío.
    - Fila de tokens compartida: se conserva mientras otra connection
      la referencie (mismo usuario, multi-PC: cae con la última
      referencia); si la referencia superviviente es de OTRO usuario
      jamás se toca (aislamiento Fase 3), con independencia del
      `owner` registrado.
    - Separado de `POST /privacy/erase` F-C2 (por instalación): este
      borra al usuario y TODAS sus instalaciones; aquel borra una
      instalación sin tocar al usuario. No fusionar.
    """
    if not user_id:
        raise ValueError("erase requires user_id")
    services = services if isinstance(services, dict) else {}
    # 1. Instalaciones adoptadas por el usuario.
    list_owned = getattr(installations, "list_for_user", None)
    owned = list(list_owned(user_id)) if callable(list_owned) else []
    # 2. Conexiones del usuario (marcadas suyas o sin marca en
    # instalación suya). Borrado local primero; la fila de tokens cae
    # solo sin más referencias y sin dueño ajeno.
    pairs = []
    if connections is not None:
        list_mine = getattr(connections, "list_for_user", None)
        if callable(list_mine):
            pairs = list(list_mine(user_id, owned))
    conns_n = toks_n = 0
    pending: dict = {}
    for iid, provider in sorted(set(pairs)):
        entry = None
        try:
            entry = connections.load(iid, provider)
        except Exception:
            entry = None
        if not isinstance(entry, dict):
            continue
        account_data = entry.get("account")
        puid = (account_data.get("provider_user_id")
                if isinstance(account_data, dict) else None)
        account = None
        stored = None
        if puid and tokens is not None:
            try:
                account = Account(
                    provider=Provider(provider),
                    provider_user_id=puid,
                    display_name=(account_data.get("display_name") or ""),
                    scopes=tuple(account_data.get("scopes") or ()))
            except ValueError:
                account = None  # proveedor desconocido: la conexión cae,
            if account is not None:  # la fila de tokens se conserva.
                try:
                    stored = tokens.load(account)
                except Exception:
                    stored = None
        try:
            connections.delete(iid, provider)
        except Exception:
            continue
        conns_n += 1
        if stored is not None and account is not None and puid:
            try:
                refs = connections.list_referencing(provider, puid)
            except Exception:
                refs = [iid]  # ante duda, conservar la fila.
            owner_ok = False
            try:
                owner_ok = tokens.owner_of(provider, puid) in (
                    None, user_id)
            except Exception:
                owner_ok = False
            if not refs and owner_ok:
                try:
                    tokens.delete(account)
                except Exception:
                    pass
                else:
                    toks_n += 1
            pending.setdefault((provider, puid), stored)
    # 3. Sesiones: las de sus instalaciones + las marcadas suyas (el
    # bearer llamante cae aquí, como en /privacy/erase: la respuesta
    # 200 ya está calculada).
    sess_n = 0
    if sessions is not None:
        for iid in owned:
            remover = getattr(sessions, "delete_for_installation", None)
            if callable(remover):
                try:
                    sess_n += remover(iid) or 0
                except Exception:
                    pass
        mine = getattr(sessions, "delete_for_user", None)
        if callable(mine):
            try:
                sess_n += mine(user_id) or 0
            except Exception:
                pass
    if user_sessions is not None and user_sessions is not sessions:
        mine = getattr(user_sessions, "delete_for_user", None)
        if callable(mine):
            try:
                sess_n += mine(user_id) or 0
            except Exception:
                pass
    # 4. Transacciones pendientes de sus instalaciones.
    txn_n = 0
    if transactions is not None:
        remover = getattr(transactions, "delete_for_installation", None)
        if callable(remover):
            for iid in owned:
                try:
                    txn_n += remover(iid) or 0
                except Exception:
                    pass
    # 5. Filas de instalación y de usuario.
    inst_n = 0
    if installations is not None:
        for iid in owned:
            try:
                installations.delete(iid)
            except Exception:
                continue
            inst_n += 1
    user_n = 0
    remover = getattr(users, "delete", None)
    if callable(remover):
        try:
            if remover(user_id):
                user_n = 1
        except Exception:
            pass
    # 6. Revoke remoto best-effort (una vez por fila de tokens).
    revoked: dict = {}
    for (provider, _puid), stored in pending.items():
        service = services.get(provider)
        provider_obj = getattr(service, "_provider", None)
        ok = True
        attempted = False
        for token in (stored.access_token, stored.refresh_token):
            if not token:
                continue
            attempted = True
            try:
                if provider_obj is None:
                    ok = False
                else:
                    provider_obj.revoke(token)
            except AppError:
                ok = False  # best-effort: el borrado local ya ocurrió
            except Exception:
                ok = False
        revoked[provider] = ok if attempted else True
    return {"erased": {"users": user_n, "installations": inst_n,
                       "sessions": sess_n, "connections": conns_n,
                       "tokens": toks_n, "transactions": txn_n},
            "revoked": revoked}


def _installation_providers(connections, installation_id: str,
                            services) -> list:
    """Proveedores con connection en la instalacion (limpieza por usuario).

    Prefiere `providers_for_installation` del store; sin ese metodo cae a
    las claves de `services` (suficiente en prod: las connections solo
    nacen via ConnectService). Sin stores = lista vacia, sin efectos.
    """
    getter = getattr(connections, "providers_for_installation", None)
    if callable(getter):
        try:
            return sorted(getter(installation_id) or [])
        except Exception:
            return []
    if isinstance(services, dict):
        return sorted(services)
    return []


def _load_account(provider: str, entry: dict):
    """`(Account, provider_user_id)` o `(None, None)` si no hay cuenta
    valida (proveedor desconocido: la connection cae igual, la fila de
    tokens se deja en paz)."""
    data = entry.get("account") if isinstance(entry, dict) else None
    puid = (data.get("provider_user_id")
            if isinstance(data, dict) else None)
    if not puid:
        return None, None
    try:
        account = Account(
            provider=Provider(provider),
            provider_user_id=puid,
            display_name=data.get("display_name") or "",
            scopes=tuple(data.get("scopes") or ()))
    except ValueError:
        return None, None  # proveedor desconocido
    return account, puid


def _entry_owner(provider: str, entry: dict, tokens):
    """Dueno de la connection: marca `user_id`, o `owner` del token como
    respaldo (legado sin marca). None = fila sin dueno (pre-usuarios)."""
    marker = entry.get("user_id") if isinstance(entry, dict) else None
    if marker is not None:
        return marker
    account, puid = _load_account(provider, entry)
    if account is None or tokens is None:
        return None
    try:
        return tokens.owner_of(provider, puid)
    except Exception:
        return None


def _delete_guarded(provider: str, installation_id: str, *,
                    services, connections, tokens, pending) -> tuple:
    """Borrado protegido de UNA connection (semantica disconnect F-C2).

    DELETE local primero (tokens copiados en memoria para el revoke
    posterior); la fila de tokens cae solo sin mas referencias en
    ninguna instalacion (cuenta compartida multi-PC: cae con la ultima
    referencia). `pending` acumula `(provider, puid) -> TokenPair` para
    el revoke best-effort. Devuelve `(connections, tokens)`.
    """
    try:
        entry = connections.load(installation_id, provider)
    except Exception:
        return 0, 0
    if not isinstance(entry, dict):
        return 0, 0
    account, puid = _load_account(provider, entry)
    stored = None
    if account is not None and tokens is not None:
        try:
            stored = tokens.load(account)
        except Exception:
            stored = None
    try:
        connections.delete(installation_id, provider)
    except Exception:
        return 0, 0
    toks = 0
    if stored is not None and account is not None and puid:
        try:
            refs = connections.list_referencing(provider, puid)
        except Exception:
            refs = [installation_id]  # ante duda, conservar la fila
        if not refs:
            try:
                tokens.delete(account)
            except Exception:
                pass
            else:
                toks = 1
        pending.setdefault((provider, puid), stored)
    return 1, toks


def _revoke_pending(pending: dict, services) -> dict:
    """Revoke remoto best-effort (F-030: el borrado local ya ocurrio; un
    fallo de red jamas resucita estado). Solo conteos y bools."""
    services = services if isinstance(services, dict) else {}
    revoked: dict = {}
    for (provider, _puid), stored in pending.items():
        service = services.get(provider)
        provider_obj = getattr(service, "_provider", None)
        ok, attempted = True, False
        for token in (stored.access_token, stored.refresh_token):
            if not token:
                continue
            attempted = True
            try:
                if provider_obj is None:
                    ok = False
                else:
                    provider_obj.revoke(token)
            except AppError:
                ok = False
            except Exception:
                ok = False
        revoked[provider] = ok if attempted else True
    return revoked


def purge_foreign_connections(user_id: str, installation_id: str, *,
                              services, connections, tokens) -> dict:
    """Limpieza al entrar (decision [D] 2026-10-26): tras autenticar y
    ANTES de adoptar, elimina las connections de la instalacion llamante
    con dueno DISTINTO al entrante (borrado protegido + revoke
    best-effort, como un disconnect). El entrante NUNCA hereda enlaces
    ajenos. Filas SIN dueno (legado NULL) y del MISMO usuario: intactas
    (las primeras las adopta `adopt_installation` despues).

    Contrato: `{"cleaned": {"connections": n, "tokens": n},
    "revoked": {provider: bool}}`. Solo conteos y bools: jamas tokens,
    secretos ni IDs. Sin instalacion o sin stores = ceros, sin efectos.
    """
    zero = {"cleaned": {"connections": 0, "tokens": 0}, "revoked": {}}
    if not user_id or not installation_id or connections is None:
        return zero
    conns_n = toks_n = 0
    pending: dict = {}
    for provider in _installation_providers(connections, installation_id,
                                            services):
        try:
            entry = connections.load(installation_id, provider)
        except Exception:
            continue
        if not isinstance(entry, dict):
            continue
        owner = _entry_owner(provider, entry, tokens)
        if owner is None or owner == user_id:
            continue
        cleared = _delete_guarded(
            provider, installation_id, services=services,
            connections=connections, tokens=tokens, pending=pending)
        conns_n += cleared[0]
        toks_n += cleared[1]
    return {"cleaned": {"connections": conns_n, "tokens": toks_n},
            "revoked": _revoke_pending(pending, services)}


def clean_installation_connections(installation_id: str, *, services,
                                   connections, tokens) -> dict:
    """Limpieza total Managed de UNA instalacion (logout, [D] 2026-10-26):
    borra TODAS sus connections (borrado protegido + revoke best-effort)
    sin tocar otras instalaciones (multi-PC del mismo usuario intacto)
    ni sesiones de instalacion. Mismo contrato que
    `purge_foreign_connections` (solo conteos y bools). Idempotente:
    instalacion sin connections = ceros.
    """
    zero = {"cleaned": {"connections": 0, "tokens": 0}, "revoked": {}}
    if not installation_id or connections is None:
        return zero
    conns_n = toks_n = 0
    pending: dict = {}
    for provider in _installation_providers(connections, installation_id,
                                            services):
        cleared = _delete_guarded(
            provider, installation_id, services=services,
            connections=connections, tokens=tokens, pending=pending)
        conns_n += cleared[0]
        toks_n += cleared[1]
    return {"cleaned": {"connections": conns_n, "tokens": toks_n},
            "revoked": _revoke_pending(pending, services)}
