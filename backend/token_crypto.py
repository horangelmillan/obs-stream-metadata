"""Cifrado de tokens en reposo (F-C1, T-063): AEAD app-layer con Fernet.

Formato versionado inline en las columnas TEXT existentes de `tokens`:

    v1:<token-Fernet>

- `save` cifra siempre con la clave primaria; `load` acepta celdas `v1:`
  (descifra con primaria + previous via MultiFernet) y celdas legacy en
  claro (doble lectura para no romper sesiones vivas tras el deploy).
- Descifrado imposible (clave erronea, corrupcion) -> `TokenCryptoError`
  fail-closed: nunca se devuelve basura como bearer (la frontera HTTP lo
  mapea a 500 generico, sin detalle ni valores).
- La clave jamas entra a la DB: solo vive en Secret Manager (ficheros,
  patron T-057/T-058) y en memoria del proceso. Nombres de secreto:
  `TOKEN_ENCRYPTION_KEY` (obligatorio en prod) y
  `TOKEN_ENCRYPTION_KEY_PREVIOUS` (opcional, solo durante rotacion).
- Sin criptografia propia: solo primitivas estandar de lib `cryptography`
  (Fernet = AES-128-CBC + HMAC-SHA256, IV `os.urandom`, `InvalidToken`
  fail-closed). Los tokens caben en memoria: Fernet es apto.
- `expires_in`/`scope` siguen en claro (metadatos no sensibles, necesarios
  para `ensure_fresh_token` sin descifrar). `refresh_token` vacio (ausencia
  real, p. ej. YouTube sin offline) se preserva vacio, sin cifrar.
"""
from __future__ import annotations

from backend.kernel import Account
from backend.ports import TokenPair, TokenStore

CIPHERTEXT_PREFIX = "v1:"

TOKEN_KEY_NAME = "TOKEN_ENCRYPTION_KEY"
TOKEN_PREVIOUS_KEY_NAME = "TOKEN_ENCRYPTION_KEY_PREVIOUS"


class TokenCryptoError(Exception):
    """Fallo de cifrado/descifrado o clave invalida (fail-closed)."""


class TokenCipher:
    """AEAD simetrico sobre celdas de texto (una por token)."""

    def __init__(self, primary: bytes | str,
                 previous: bytes | str | None = None) -> None:
        from cryptography.fernet import Fernet
        try:
            fernets = [Fernet(primary)]
            if previous:
                fernets.append(Fernet(previous))
        except ValueError as exc:
            raise TokenCryptoError(
                "token encryption key invalid "
                "(expected Fernet.generate_key() format)") from exc
        if len(fernets) == 1:
            self._enc = fernets[0]
            self._dec = fernets[0]
        else:
            from cryptography.fernet import MultiFernet
            self._enc = fernets[0]
            self._dec = MultiFernet(fernets)

    @classmethod
    def from_secret_store(cls, secrets) -> "TokenCipher":
        """Construye desde un SecretStore (nombres en errores, nunca valores)."""
        from backend.prodstores import ProdstoresError
        primary = secrets.get(TOKEN_KEY_NAME)
        if not primary:
            raise ProdstoresError(
                "production requires token encryption key: "
                f"mount {TOKEN_KEY_NAME} via SECRET_DIR(S)")
        previous = secrets.get(TOKEN_PREVIOUS_KEY_NAME)
        try:
            return cls(primary, previous)
        except TokenCryptoError as exc:
            raise ProdstoresError(
                f"invalid {TOKEN_KEY_NAME} format "
                "(expected Fernet.generate_key() output)") from exc

    def encrypt(self, plain: str) -> str:
        if not plain:
            return ""
        return CIPHERTEXT_PREFIX + self._enc.encrypt(
            plain.encode("utf-8")).decode("ascii")

    def decrypt(self, cell: str) -> str:
        if not cell or not cell.startswith(CIPHERTEXT_PREFIX):
            return cell
        from cryptography.fernet import InvalidToken
        try:
            return self._dec.decrypt(
                cell[len(CIPHERTEXT_PREFIX):].encode("ascii")).decode("utf-8")
        except (InvalidToken, ValueError) as exc:
            raise TokenCryptoError(
                "token cell undecryptable (wrong key or corrupted)") from exc


def needs_upgrade(cell: str) -> bool:
    """True si la celda es legacy en claro (no vacia y sin prefijo `v1:`)."""
    return bool(cell) and not cell.startswith(CIPHERTEXT_PREFIX)


class EncryptedTokenStore(TokenStore):
    """Decorador del port TokenStore: cifra al guardar, doble lectura al leer.

    Sin `DEVELOPMENT_ONLY`: apto para `production` (gates T-053 lo aceptan).
    El `inner` sigue siendo el dueño del SQL (PgTokenStore en prod).
    """

    def __init__(self, inner: TokenStore, cipher: TokenCipher) -> None:
        self._inner = inner
        self._cipher = cipher

    def save(self, account: Account, tokens: TokenPair) -> None:
        self._inner.save(account, TokenPair(
            self._cipher.encrypt(tokens.access_token),
            self._cipher.encrypt(tokens.refresh_token),
            tokens.expires_in, tokens.scope))

    def load(self, account: Account) -> TokenPair | None:
        found = self._inner.load(account)
        if found is None:
            return None
        return TokenPair(self._cipher.decrypt(found.access_token),
                         self._cipher.decrypt(found.refresh_token),
                         found.expires_in, found.scope)

    def delete(self, account: Account) -> None:
        self._inner.delete(account)
