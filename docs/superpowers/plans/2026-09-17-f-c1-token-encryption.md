# F-C1 Cifrado de tokens en reposo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `PgTokenStore` deja de guardar `access_token`/`refresh_token` en claro; AEAD app-layer con clave en Secret Manager, doble lectura legacy/cifrado, backfill y rotación documentada.

**Architecture:** Nuevo módulo `backend/token_crypto.py` con `TokenCipher` (Fernet/MultiFernet de lib `cryptography`, sin crypto propia) + `EncryptedTokenStore` (decorador del port `TokenStore`); el wiring de producción (`app._production_wiring`) envuelve `PgTokenStore`. Formato versionado inline `v1:<fernet-token>` en las columnas TEXT existentes; migraciones y resto de tablas intactos.

**Tech Stack:** Python 3.13 stdlib + `cryptography==50.0.1` (nueva dep, ruedas abi3 manylinux/win, funciona en `python:3.13-slim`) + `psycopg==3.3.5`; PostgreSQL estándar (sin extensiones); Secret Manager montado como ficheros (patrón T-057/T-058).

**Spec:** `docs/FASES-COMERCIAL.md` §F-C1; `docs/BACKLOG.md` T-063.

## Global Constraints

- Secretos solo vía `SecretStore`/ficheros; jamás en repo, logs, respuestas, C++ o instalador (`docs/SECURITY.md`).
- `public_body` intacto: ningún detalle interno en respuestas (`backend/errors.py`; `http_server.py:315` mapea excepciones desconocidas a 500 genérico).
- `RedactingFilter` cubre lo nuevo; CI secret-scan en verde (regex `ci-phase0.yml:48-51`; en tests usar `TokenPair` posicional con valores `fk-*`, jamás `access_token="..."` literales ≥8 chars).
- Sin cambios a OAuth/sesiones/flujos Managed salvo lo estrictamente necesario (`backend/oauth.py` intacto; el decorador implementa el port `TokenStore` existente).
- Tablas `sessions`/`transactions`/`connections`/`installations` fuera de alcance (no se cifran en F-C1; solo `tokens.access_token`/`refresh_token`).
- Commits en español, sin commit sin autorización del usuario.

## Decisión documentada (F-C1 §2.3; consulta 2026-09-17)

- (a) **Fernet + clave en Secret Manager — ELEGIDA.** AEAD vigente (AES-128-CBC + HMAC-SHA256, IV `os.urandom`, `InvalidToken` fail-closed; https://cryptography.io/en/latest/fernet/, docs `/pyca/cryptography` vía context7). `MultiFernet` da rotación con doble lectura nativa. Una sola dep con ruedas binarias (sin compilador en slim/CI). Coste 0, latencia 0, encaja en `FileSecretStore`/`CompositeSecretStore` existente. Cumple el requisito explícito de Google ("always store encrypted tokens at rest", https://developers.google.com/identity/protocols/oauth2/policies).
- (b) **KMS envelope — DESCARTADA para F-C1 (vía futura).** KEK nunca sale de KMS, audit logs, IAM con la SA existente (`roles/cloudkms.cryptoKeyEncrypterDecrypter`, auth sin secretos vía ADC; https://docs.cloud.google.com/kms/docs/envelope-encryption). Pero: nueva dep `google-cloud-kms` + permisos + latencia por `Decrypt` (o cachear DEK, que reintroduce (a)); coste $0.06/mes/versión + $0.03/10k ops (https://cloud.google.com/kms/pricing, precios vigentes 2025-03-17); sobredimensionada para cientos de filas con 1 instancia. Reevaluar al multi-instancia/gran escala.
- (c) **pgcrypto en PG — DESCARTADA con razones explícitas.** La clave viajaría en cada query (GUC/SQL, visible en logs/`pg_stat`); el descifrado corre dentro del proceso postgres (un fallo es shell en el host DB); CVE-2026-2005 en la ruta PGP (parchear 18.3/17.9/16.13/15.17/14.22; https://thebuild.com/blog/twenty-years-in-pgcrypto/, 2026-05-13); Neon Free puede restringir extensiones; rotación dolorosa. Consenso 2026: crypto app-layer, claves fuera de la DB (https://www.buildwithmatija.com/blog/postgres-encryption-at-rest-vs-e2e-where-data-leaks, 2026-06-04; https://dhdtech.io/blog/searchable-encryption-that-ships-field-level-2026-cto-playbook/, 2026-07-19).
- (c2) **Tink+KMS — DESCARTADA.** Misma infraestructura que (b) con más superficie API; sin beneficio a esta escala.
- No hay empate ni riesgo arquitectónico: se implementa (a) sin pedir decisión.

## Formato y comportamientos (fijan los tests)

- `CIPHERTEXT_PREFIX = "v1:"`; celda cifrada = `"v1:" + Fernet.encrypt(plaintext.encode()).decode()` (token Fernet ASCII, cabe en TEXT).
- `save`: cifra siempre con la clave primaria; `load`: si la celda empieza por `v1:` descifra (probando primaria y previous vía `MultiFernet`); si no, devuelve legacy en claro (doble lectura). `delete`: intacto (delega).
- Descifrado imposible (clave errónea, corrupción) → `TokenCryptoError` fail-closed (burbujea a 500 genérico vía `http_server.py:315`; log interno sin valores). Nunca devolver basura como bearer.
- Clave: `Fernet.generate_key()` (32B urlsafe-b64). Secretos nuevos: `TOKEN_ENCRYPTION_KEY` (requerida en prod) + `TOKEN_ENCRYPTION_KEY_PREVIOUS` (opcional, solo rotación). Nombres en errores, nunca valores.
- Migración `002_token_ciphertext.sql`: sin cambio DDL (TEXT ya contiene `v1:`; la clave jamás entra a la DB por diseño) — el fichero documenta el contrato y registra la versión en `schema_migrations`. `test_pg.py::MigrationsTest::test_discover_ordered` pasa a `["001","002"]`.
- Rotación: nueva clave → `PREVIOUS`=antigua, `KEY`=nueva → deploy → backfill re-cifra todo a primaria → retirar `PREVIOUS` (detalle operativo en `docs/DEPLOYMENT.md`).

---

### Task 1: Dependencia `cryptography` pineada

**Files:**
- Modify: `backend/requirements.txt`
- Test: instalación local + import

**Interfaces:**
- Consumes: nada.
- Produces: `cryptography==50.0.1` disponible en dev/CI/slim (rueda abi3; `Dockerfile.backend` ya hace `pip install -r requirements.txt`, sin cambios).

- [ ] **Step 1: Añadir el pin**

```text
cryptography==50.0.1
```

con comentario (ruedas binarias, sin compilador; AEAD Fernet para F-C1).

- [ ] **Step 2: Instalar y verificar import**

Run: `python -c "from cryptography.fernet import Fernet, MultiFernet; print(Fernet.generate_key().decode()[:8])"`
Expected: PASS (imprime 8 chars de clave fresca, distinto en cada run).

---

### Task 2: `backend/token_crypto.py` — cipher + decorador (TDD rojo primero)

**Files:**
- Create: `backend/token_crypto.py`
- Test: `backend/tests/test_token_crypto.py`

**Interfaces:**
- Consumes: `backend/ports.py` (`TokenStore`, `TokenPair`), `backend/kernel.py` (`Account`), `backend/prodstores.py` (`ProdstoresError`).
- Produces: `TokenCryptoError(Exception)`, `CIPHERTEXT_PREFIX="v1:"`, `TOKEN_KEY_NAME="TOKEN_ENCRYPTION_KEY"`, `TOKEN_PREVIOUS_KEY_NAME="TOKEN_ENCRYPTION_KEY_PREVIOUS"`, `class TokenCipher` con `from_secret_store(secrets)`, `encrypt(plain)->str`, `decrypt(cell)->str`; `class EncryptedTokenStore(TokenStore)` con `__init__(inner, cipher)`, `save/load/delete` (sin `DEVELOPMENT_ONLY`: apto para prod).

- [ ] **Step 1: Escribir el test rojo** (`backend/tests/test_token_crypto.py`):

```python
"""Tests F-C1 (T-063): AEAD de tokens en reposo (Fernet + Secret Manager).

Disciplina secret-scan: TokenPair posicional, valores `fk-*`; claves
Fernet generadas en runtime, jamás literales.
"""
import unittest

from backend.kernel import Account, Provider
from backend.ports import TokenPair
from backend.stores import InMemoryTokenStore
from backend.token_crypto import (CIPHERTEXT_PREFIX, EncryptedTokenStore,
                                  TokenCipher, TokenCryptoError)


def _account(uid="UC9z"):
    return Account(provider=Provider.YOUTUBE, provider_user_id=uid,
                   display_name="Canal 9z", scopes=("s1",))


def _pair():
    return TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")


def _cipher():
    from cryptography.fernet import Fernet
    return TokenCipher(Fernet.generate_key())


def _wrapped():
    return EncryptedTokenStore(InMemoryTokenStore(), _cipher())


class CipherRoundtripTest(unittest.TestCase):
    def test_roundtrip(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        store.save(_account(), _pair())
        raw = inner.load(_account())
        self.assertTrue(raw.access_token.startswith(CIPHERTEXT_PREFIX))
        self.assertTrue(raw.refresh_token.startswith(CIPHERTEXT_PREFIX))
        self.assertNotIn("fk-acc-9z", raw.access_token)
        self.assertNotIn("fk-ref-9z", raw.refresh_token)
        got = store.load(_account())
        self.assertEqual(got.access_token, "fk-acc-9z")
        self.assertEqual(got.refresh_token, "fk-ref-9z")
        self.assertEqual(got.expires_in, 3600)
        self.assertEqual(got.scope, "s1")

    def test_wrong_key_fails_closed(self):
        store = _wrapped()
        store.save(_account(), _pair())
        from cryptography.fernet import Fernet
        other = EncryptedTokenStore(store._inner,
                                    TokenCipher(Fernet.generate_key()))
        with self.assertRaises(TokenCryptoError):
            other.load(_account())

    def test_tampered_cell_fails_closed(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        store.save(_account(), _pair())
        raw = inner.load(_account())
        inner.save(_account(), TokenPair(raw.access_token[:-4] + "AAAA",
                                         raw.refresh_token, 3600, "s1"))
        with self.assertRaises(TokenCryptoError):
            store.load(_account())


class LegacyDualReadTest(unittest.TestCase):
    def test_legacy_plaintext_still_reads(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account(), _pair())  # fila pre-F-C1, en claro
        got = store.load(_account())
        self.assertEqual(got.access_token, "fk-acc-9z")
        self.assertEqual(got.refresh_token, "fk-ref-9z")

    def test_mixed_rows_coexist(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account("legacy"), _pair())
        store.save(_account("nuevo"), _pair())
        self.assertEqual(store.load(_account("legacy")).access_token,
                         "fk-acc-9z")
        self.assertTrue(inner.load(_account("nuevo")).access_token
                        .startswith(CIPHERTEXT_PREFIX))

    def test_save_upgrades_legacy_to_ciphertext(self):
        inner = InMemoryTokenStore()
        store = EncryptedTokenStore(inner, _cipher())
        inner.save(_account(), _pair())
        store.save(_account(), _pair())  # p. ej. refresh tras deploy
        self.assertTrue(inner.load(_account()).access_token
                        .startswith(CIPHERTEXT_PREFIX))

    def test_missing_returns_none(self):
        self.assertIsNone(_wrapped().load(_account("nadie")))

    def test_delete_delegates(self):
        store = _wrapped()
        store.save(_account(), _pair())
        store.delete(_account())
        self.assertIsNone(store.load(_account()))


class RotationTest(unittest.TestCase):
    def test_previous_key_reads_and_resave_upgrades(self):
        from cryptography.fernet import Fernet
        old_raw, new_raw = Fernet.generate_key(), Fernet.generate_key()
        old_store = EncryptedTokenStore(InMemoryTokenStore(),
                                        TokenCipher(old_raw))
        old_store.save(_account(), _pair())
        inner = old_store._inner
        rotated = EncryptedTokenStore(inner, TokenCipher(new_raw, old_raw))
        self.assertEqual(rotated.load(_account()).access_token, "fk-acc-9z")
        rotated.save(_account(), _pair())
        solo_new = EncryptedTokenStore(inner, TokenCipher(new_raw))
        self.assertEqual(solo_new.load(_account()).refresh_token, "fk-ref-9z")


class KeyLoadingTest(unittest.TestCase):
    def test_from_secret_store_and_missing_fails_fast(self):
        import tempfile
        from backend.prodstores import FileSecretStore, ProdstoresError
        from cryptography.fernet import Fernet
        with tempfile.TemporaryDirectory() as tmp:
            with open(tmp + "/TOKEN_ENCRYPTION_KEY", "w") as handle:
                handle.write(Fernet.generate_key().decode())
            cipher = TokenCipher.from_secret_store(FileSecretStore(tmp))
            self.assertEqual(cipher.decrypt(cipher.encrypt("fk-x-9z")),
                             "fk-x-9z")
        import os as _os
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ProdstoresError) as ctx:
                TokenCipher.from_secret_store(FileSecretStore(tmp))
            message = str(ctx.exception)
            self.assertIn("TOKEN_ENCRYPTION_KEY", message)

    def test_malformed_key_fails_fast(self):
        with self.assertRaises(TokenCryptoError):
            TokenCipher("no-es-una-clave")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Ejecutar y verificar rojo**

Run: `python -m unittest backend.tests.test_token_crypto -v`
Expected: FAIL (`ModuleNotFoundError: backend.token_crypto`).

- [ ] **Step 3: Implementación mínima** (`backend/token_crypto.py`): `TokenCipher` con `__init__(primary, previous=None)` (validación `Fernet(key)`; `ValueError`→`TokenCryptoError`), `from_secret_store` (lee `TOKEN_ENCRYPTION_KEY` obligatorio + `PREVIOUS` opcional; ausente/malformada→`ProdstoresError`/`TokenCryptoError` con nombres, nunca valores), `encrypt`/`decrypt` (vacío→`""` para preservar semántica `refresh_token` ausente; `InvalidToken`→`TokenCryptoError` sin valores); `EncryptedTokenStore.save` (cifra access+refresh, conserva `expires_in`/`scope` en claro), `load` (despacha por prefijo `v1:`, legacy passthrough, `None`→`None`), `delete` (delega). Docstring con formato, rotación y límites (tokens pequeños en memoria: Fernet apto).

- [ ] **Step 4: Verde + suite completa**

Run: `python -m unittest backend.tests.test_token_crypto -v` → PASS; luego `python -m unittest discover -s backend/tests` → verde.

---

### Task 3: Wiring de producción (clave obligatoria + decorador)

**Files:**
- Modify: `backend/app.py` (`_production_wiring`: construye `TokenCipher.from_secret_store(secrets)` y devuelve `"tokens": EncryptedTokenStore(PgTokenStore(pool), cipher)`).
- Test: `backend/tests/test_prodstores.py` (nuevo `TokenWiringTest` con dobles `PgPool`/`run_migrations`, como `ProviderSecretsBootCheckTest`): sin clave→`ProdstoresError` nombrando `TOKEN_ENCRYPTION_KEY`; con clave→`wiring["tokens"]` es `EncryptedTokenStore` sin `DEVELOPMENT_ONLY` y roundtrip contra el doble.

**Interfaces:**
- Consumes: Task 2 (`TokenCipher`, `EncryptedTokenStore`).
- Produces: prod cifra siempre; dev (`create_app` sin wiring) intacto en claro.

- [ ] **Step 1: Test rojo** (falla: `_production_wiring` no exige clave).
- [ ] **Step 2: Implementación** (import local dentro de `_production_wiring`, como `PgPool`; el check de clave ocurre antes de abrir el pool — fail-fast sin red).
- [ ] **Step 3: Verde** (`test_prodstores` + suite).

---

### Task 4: Migración `002_token_ciphertext.sql` + runner intacto

**Files:**
- Create: `backend/migrations/002_token_ciphertext.sql` (comentario-contrato: formato `v1:` app-layer, sin DDL — TEXT ya lo contiene, la clave jamás entra a la DB; + `SELECT 1;` para que el runner ejecute una transacción válida).
- Modify: `backend/tests/test_pg.py` (`test_discover_ordered` → `["001","002"]`; `fresh_db` asserts `applied == ["001","002"]`).
- Test: `MigrationsTest` + nuevo test PG (solo con servidor; skip local): `EncryptedTokenStore(PgTokenStore)` roundtrip + fila legacy insertada en crudo vía `PgTokenStore` sin decorar → dual-read OK.

- [ ] **Step 1: Tests rojos** (versions esperan `["001","002"]`).
- [ ] **Step 2: Fichero SQL + test PG dual-read.**
- [ ] **Step 3: Verde** (local: skips PG por diseño; CI con servicio `postgres:18` lo ejecuta real).

---

### Task 5: Backfill idempotente + leak-guard extendido

**Files:**
- Create: `tools/backfill_token_crypto.py` (lee `STREAM_META_BACKEND_DATABASE_URL` + `SECRET_DIR(S)` del entorno; recorre `tokens` en lotes; solo celdas sin prefijo `v1:`; `--dry-run` por defecto; conteos `legacy/converted/already/skipped`; jamás loguea valores — solo conteos y `provider`).
- Modify: `backend/tests/test_leak_guard.py` (canario cifrado: `TokenCipher.encrypt(CANARY)` no contiene el canario; `redact_text`/`public_body` no lo exponen).

- [ ] **Step 1: Tests/extensión rojos.**
- [ ] **Step 2: Script + tests.**
- [ ] **Step 3: Verde + secret-scan local** (replicar regex CI con `grep -rEI`).

---

### Task 6: Docs operativas (rotación, custodia, inventario)

**Files:**
- Modify: `docs/DEPLOYMENT.md` (montaje `TOKEN_ENCRYPTION_KEY[:PREVIOUS]` 1-secreto-por-directorio + `SECRET_DIRS`; procedimiento de rotación en 4 pasos; rollback = forward-fix documentado por ventana de cutover).
- Modify: `docs/SECURITY.md` (línea F-C1: AEAD Fernet, clave solo Secret Manager/memoria, fail-closed).
- Modify: `docs/PRIVACY.md` (fila `tokens`: contenido = ciphertext `v1:` + metadatos en claro; sin cambio de retención/borrado).
- Modify: `docs/BACKLOG.md` (T-063 → hecha al cierre, con aceptación cumplida).

---

### Task 7: Puerta de evidencia F-C1 (no avanzar sin esto)

- [ ] `python -m unittest discover -s backend/tests` verde + selfcheck C++ OK (sin cambios C++ esperados; recompilar no necesario).
- [ ] Dump real con solo ciphertext: `pg_dump`/psql `SELECT provider, provider_user_id, substr(access_token,1,8), ...` mostrando prefijo `v1:` y ausencia de `fk-*`/tokens previos (valores redactados en el informe).
- [ ] Deploy Cloud Run (mismo procedimiento: Cloud Build + update + tag) con el secreto nuevo montado; sonda prod: connect + Apply reales (YouTube y/o Kick) contra filas cifradas; verificación de doble lectura (una fila legacy pre-deploy sigue funcionando tras el deploy, luego backfill).
- [ ] Secret-scan CI verde.
- [ ] Informe final + T-063 hecha. Sin commit sin autorización.

## Self-Review

- Cobertura: roundtrip ✓, clave errónea ✓, legacy→v1 ✓, dual-read ✓, rotación ✓, dump ✓, deploy+sonda ✓, scan ✓. `public_body`/`RedactingFilter` ✓ (Task 5). Tablas no tocadas ✓ (Task 4 solo añade fichero de versión). Sin crypto propia ✓.
- Sin placeholders: cada test trae código exacto; las rutas y nombres de secretos son literales.
- Tipos: `EncryptedTokenStore(inner: TokenStore, cipher: TokenCipher)`; `TokenCipher(primary: bytes|str, previous=None)`; `TokenPair`/`Account` existentes sin cambios.
