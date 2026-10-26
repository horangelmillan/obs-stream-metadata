# Cuentas Managed v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cerrar Cuentas Managed v1 dejando Fases 1–5 como hechas con evidencia y ejecutar la Fase 6 pendiente (E2E multi-PC + backup/rollback) con gates de despliegue.

**Architecture:** Enfoque A del spec: cada cuenta Managed pertenece a un `user` desde su creación; adopción de instalaciones anónimas al primer login/signup sin reset; auth doble (sesión instalación + sesión usuario) en rutas Managed; erase de cuenta en cascada separado de F-C2.

**Tech Stack:** Python stdlib backend (`backend/users.py`, `backend/http_server.py`, stores + `backend/migrations/003_users.sql`), PBKDF2-SHA256 600 000 iteraciones con salt 16 B, Qt6/C++ plugin (`src/backend_auth.*`, `src/metadata*`), unittest stdlib.

**Spec:** `docs/superpowers/specs/2026-10-26-managed-user-accounts-design.md`

## Global Constraints

- Enfoque A vinculante: tokens del usuario desde el día uno; sin cuentas Managed huérfanas tras la migración (spec §2).
- Hash PBKDF2-SHA256, formato `pbkdf2-sha256$iter$salt_b64$hash_b64`, 600 000 iteraciones, salt 16 B; jamás contraseñas ni hashes en logs (spec §§3–4, `RedactingFilter`).
- Sin email en v1: sin campo email, sin verificación, sin reset; documentar sin recuperación en UX (spec §7).
- Signup web + adopción sin reset ni downtime, sin regenerar `backendInstall` (spec §§3, 9).
- Gate login previo a Managed en el plugin; sesiones de usuario solo en memoria, nunca en `accounts.json` ni DPAPI (spec §5).
- Independent intacto: BYO-app/DPAPI sin login, sin cambios en sus flujos (spec §5).
- Erase de cuenta (`POST /users/erase`) separado de `POST /privacy/erase` (F-C2): local primero, revoke después, idempotente, solo conteos (spec §§5–6).
- TDD rojo→verde en modelo, endpoints, auth doble, rate-limit login, adopción y erase (spec §8).
- Migración `003_users.sql` ordenada tras las vigentes, una transacción por fichero; `pg_dump` antes de aplicarla en cada entorno; rollback = restaurar backup + URL anterior (spec §8).
- Validación por fase: `python -m unittest discover -s backend/tests`, build/selfcheck del plugin, secret-scan sin secretos.

---

### Task 1: Fase 1 — Modelo `users` + migración 003 [CERRADA]

**Files:**
- Create: `backend/users.py` (hecho en `0f015a3`)
- Create: `backend/migrations/003_users.sql` (hecho en `0f015a3`)
- Test: `backend/tests/test_users_model.py` (hecho en `0f015a3`)

**Interfaces:**
- Consumes: `backend/errors.py` (`AppError`, `ErrorCode`), `backend/kernel.py` (`Account`, `Provider`).
- Produces: `hash_password(plain: str) -> str`, `verify_password(plain: str, stored: str) -> bool`, `validate_username(raw) -> str`, `validate_password(raw) -> str`, `adopt_installation(store, install_id: str, user_id: str) -> dict`, constantes `PASSWORD_ITERATIONS = 600_000`, `USER_SESSION_TTL_S = 1800`, `LOGIN_PER_IP_LIMIT = 10`, `LOGIN_PER_USER_LIMIT = 5`, `SIGNUP_PER_IP_LIMIT = 10`.

**Evidencia de cierre [D]:** commit `0f015a3` (`git show --stat --oneline 0f015a3`), mensaje registra Fases 1–5 con suites 299→337 OK y selfcheck f4/f5 OK.

- [ ] **Step 1: Registrar lo implementado (sin re-tocar código)**

Hecho en `0f015a3`: `backend/users.py` con modelo `User` inmutable (`id`, `username`, `password_hash`, `created_at`, `updated_at`, reservas `totp_secret`/`oauth_provider`/`oauth_subject` a nulo), validación username (strip, 3–32, `[A-Za-z0-9._-]`), password 12–128, y rate-limits login 10/10 min por IP + 5/10 min por username, signup 10/10 min por IP.

```python
def hash_password(plain: str) -> str:
    if not plain:
        raise ValueError("empty password")
    salt = secrets.token_bytes(SALT_BYTES)
    digest = hashlib.pbkdf2_hmac("sha256", plain.encode("utf-8"), salt,
                                 PASSWORD_ITERATIONS)
    return (f"{PASSWORD_ALGORITHM}${PASSWORD_ITERATIONS}"
            f"${base64.b64encode(salt).decode()}"
            f"${base64.b64encode(digest).decode()}")
```

- [ ] **Step 2: Registrar migración 003 (sin re-tocar código)**

Hecho en `0f015a3`: `backend/migrations/003_users.sql` idempotente (`CREATE TABLE IF NOT EXISTS` + `ADD COLUMN IF NOT EXISTS`), columnas `users` + `installations.user_id`, `sessions.user_id`, `connections.user_id`, `tokens.owner_user_id` nulables para adopción diferida.

```sql
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY CHECK (length(id) > 0),
    username TEXT NOT NULL UNIQUE CHECK (length(username) > 0),
    password_hash TEXT NOT NULL CHECK (length(password_hash) > 0),
    created_at BIGINT NOT NULL DEFAULT 0,
    updated_at BIGINT NOT NULL DEFAULT 0,
    totp_secret TEXT NULL,
    oauth_provider TEXT NULL,
    oauth_subject TEXT NULL
);
```

- [ ] **Step 3: Validación registrada**

Comando de referencia (evidencia aportada en el mensaje de `0f015a3`, no re-ejecutado por el steward): `python -m unittest discover -s backend/tests` → 299→337 OK.

### Task 2: Fase 2 — Endpoints users + auth doble [CERRADA]

**Files:**
- Modify: `backend/http_server.py` (hecho en `0f015a3`)
- Modify: `backend/app.py`, `backend/stores.py`, `backend/pgstores.py`, `backend/prodstores.py`, `backend/errors.py`, `backend/token_crypto.py` (hecho en `0f015a3`)
- Test: `backend/tests/test_users_endpoints.py`, `backend/tests/test_users_dualauth.py` (hecho en `0f015a3`)

**Interfaces:**
- Consumes: `hash_password`, `verify_password`, `validate_username`, `validate_password`, `adopt_installation` de Task 1.
- Produces: rutas `POST /users/signup` (201 `{user_id, username}`, 409 si existe), `POST /users/login` (200 `{user_session, expires_in}` + adopción), `POST /users/logout` (200 idempotente), `GET /users/me` (200 `{user_id, username}`), `GET /users/signup` (HTML mínimo stdlib); auth doble en `connect`/`metadata` con 401 normalizado `managed auth required` vía cabecera `X-User-Session`.

**Evidencia de cierre [D]:** commit `0f015a3`, detalle `MANAGED_AUTH_DETAIL = "managed auth required"` visible en `backend/http_server.py` de esa revisión.

- [ ] **Step 1: Registrar endpoints (sin re-tocar código)**

Hecho en `0f015a3`: handlers con login rate-limit específico adicional al vigente, `logout`/`me` exigiendo `X-User-Session`, y página `GET /users/signup` con formulario mínimo stdlib que publica JSON a `/users/signup`:

```python
MANAGED_AUTH_DETAIL = "managed auth required"

token = self.headers.get("X-User-Session", "")
```

- [ ] **Step 2: Registrar auth doble (sin re-tocar código)**

Hecho en `0f015a3`: rutas Managed de conexión y metadata exigen sesión de instalación válida (T-044) + sesión de usuario válida; si falta cualquiera → 401 normalizado sin indicar cuál falló más allá de lo necesario; auditoría solo con identificadores no sensibles, códigos y contadores.

- [ ] **Step 3: Validación registrada**

Comando de referencia: `python -m unittest discover -s backend/tests` → incluido en el 299→337 OK citado en `0f015a3`; secret-scan sin contraseñas/hashes/tokens en logs.

### Task 3: Fase 3 — Gate login en el plugin [CERRADA]

**Files:**
- Modify: `src/backend_auth.h`, `src/backend_auth.cpp`, `src/metadata.h`, `src/metadata.cpp`, `src/metadata_dock.h`, `src/metadata_dock.cpp` (hecho en `0f015a3`)

**Interfaces:**
- Consumes: rutas Task 2 (`/users/login`, `/users/logout`, `/users/me`) + sesión de instalación T-044 vigente.
- Produces: dock con acciones Managed deshabilitadas sin login (mensaje de login, sin red ni fallback silencioso a Independent); sesión de usuario solo en memoria con revalidación al reiniciar OBS.

- [ ] **Step 1: Registrar gate (sin re-tocar código)**

Hecho en `0f015a3`: `src/backend_auth.*` gestiona la sesión de usuario en memoria; `metadata_dock` deshabilita Connect/Apply/erase Managed sin login; Independent sigue operativo sin login (ADR-008/ADR-012 intactos).

- [ ] **Step 2: Validación registrada**

Build del plugin + selfcheck f4/f5 OK según mensaje de `0f015a3` (detalle en Task 4).

### Task 4: Fase 4 — Selfcheck f4/f5 [CERRADA]

**Files:**
- Modify: `src/metadata_selfcheck.cpp` (hecho en `0f015a3`)

**Interfaces:**
- Consumes: gate de Task 3.
- Produces: cheques f4/f5 del selfcheck cubriendo el gate Managed.

- [ ] **Step 1: Registrar selfcheck (sin re-tocar código)**

Hecho en `0f015a3`: `src/metadata_selfcheck.cpp` ampliado con f4/f5; resultado OK citado en el mensaje del commit.

- [ ] **Step 2: Validación registrada**

Selfcheck f4/f5 OK [D, mensaje de `0f015a3`].

### Task 5: Fase 5 — Erase de cuenta + ajustes de suites [CERRADA]

**Files:**
- Modify: `backend/http_server.py`, `backend/stores.py`, `backend/pgstores.py`, `backend/prodstores.py` (ruta `POST /users/erase`, hecho en `0f015a3`)
- Test: `backend/tests/test_users_erase.py` + ajustes `test_facebook.py`, `test_http.py`, `test_kick.py`, `test_metadata_youtube.py`, `test_pg.py`, `test_twitch.py` (hecho en `0f015a3`)

**Interfaces:**
- Consumes: Tasks 1–2 (propiedad `user_id`/`owner_user_id`).
- Produces: `POST /users/erase` en cascada usuarios→instalaciones→sesiones→conexiones→tokens no compartidos del usuario, revoke best-effort en proveedores, idempotente, solo conteos en respuesta/logs; separado de `POST /privacy/erase` (F-C2 intacto).

- [ ] **Step 1: Registrar erase (sin re-tocar código)**

Hecho en `0f015a3`: cascada con semántica F-030/F-C2 (local primero, revoke después); filas `tokens` compartidas tratadas sin relajar F-C2.

- [ ] **Step 2: Validación registrada**

Suites 299→337 OK [D, mensaje de `0f015a3`].

### Task 6: Fase 6 — E2E multi-PC + backup/rollback [PENDIENTE]

**Files:**
- Create: `docs/VALIDATION.md` entrada nueva (la redacta `harness-steward` cuando el operador aporte evidencia; este plan no la anticipa).
- Touch: ninguno de código salvo hallazgos; si el E2E revela defectos se abre tarea nueva, no se reabre 1–5.

**Interfaces:**
- Consumes: Tasks 1–5 (v1 desplegable en staging o binario local + backend de prueba).
- Produces: acta E2E con dos PCs/instalaciones sobre el mismo usuario (adopción sin reset + aislamiento por instalación + Apply en ambas) y procedimiento backup/rollback verificado.

- [ ] **Step 1: Hacer `pg_dump` antes de aplicar la 003 en el entorno de prueba**

```bash
pg_dump "$DATABASE_URL" -F c -f backup-pre-003.dump
```

Expected: fichero `backup-pre-003.dump` creado sin exponer su contenido en logs.

- [ ] **Step 2: Aplicar migraciones en orden y comprobar 003**

```bash
python -m unittest discover -s backend/tests
```

Expected: PASS; la tabla `users` existe y las columnas `user_id`/`owner_user_id` existen como nulables.

- [ ] **Step 3: Alta y login desde PC-A con adopción sin reset**

```bash
curl -s -X POST "$BASE/users/signup" -H "Content-Type: application/json" -d "{\"username\":\"e2e-user\",\"password\":\"alongpassword123\"}"
curl -s -X POST "$BASE/users/login" -H "Content-Type: application/json" -d "{\"username\":\"e2e-user\",\"password\":\"alongpassword123\"}"
```

Expected: signup 201 `{user_id, username}`; login 200 `{user_session, expires_in}`; la instalación anónima previa de PC-A conserva `backendInstall` y conexiones/tokens (adopción, sin re-bootstrap).

- [ ] **Step 4: Adoptar PC-B al mismo usuario y verificar aislamiento + Apply en ambas**

Repetir el login de Step 3 desde la segunda instalación; luego Apply de metadata desde ambas.

Expected: ambas instalaciones comparten `user_id` pero mantienen sesiones/aislamiento propios; Apply OK en las dos; sin login → 401 `managed auth required`.

- [ ] **Step 5: Verificar rollback = restaurar backup + URL anterior**

```bash
pg_restore -d "$DATABASE_URL" backup-pre-003.dump
```

Expected: base restaurada al estado previo a 003; documentado en el acta, sin migraciones hacia atrás improvisadas.

- [ ] **Step 6: Registrar acta en `docs/VALIDATION.md`**

Solo con evidencia del operador (comando + salida o E2E registrado); `harness-steward` la transcribe, ningún ejecutor la inventa.

### Task 7: Gates de despliegue + PRIVACY [PENDIENTE]

**Files:**
- Toca solo el editor autorizado de `docs/PRIVACY.md` (no es este plan ni `harness-steward`): nuevas filas `users`, `user_id`/`owner_user_id`, §§2–9/14 y apéndice.

**Interfaces:**
- Consumes: Task 6 (E2E en verde).
- Produces: gate de deploy desbloqueado.

- [ ] **Step 1: Confirmar secreto-scan limpio sobre el diff de `0f015a3`**

```bash
git diff 0f015a3^ 0f015a3 --check
```

Expected: sin errores de formato; más barrido de secretos sin valores en el informe.

- [ ] **Step 2: Confirmar que `docs/PRIVACY.md` queda pendiente del editor autorizado**

`user_id` es el primer identificador de persona del backend Managed; su documentación descriptiva corresponde al dueño de `PRIVACY.md`, fuera del alcance de este plan.

- [ ] **Step 3: Gate de despliegue**

Sin E2E multi-PC (Task 6) en verde + `PRIVACY.md` actualizado, no hay despliegue. La infraestructura productiva y Cloud Scheduler siguen bajo sus gates vigentes (subagente GCP en solo lectura).

## Decisiones fijadas [D 2026-10-26, chat de aprobación]

1. Contraseña en v1 (sin email ni 2FA).
2. Enfoque A: tokens del usuario desde el día uno (B/C descartados).
3. Acceso web + adopción (login también desde página web sencilla; instalaciones anónimas se adoptan sin reset).
4. Fijados en implementación: PBKDF2-SHA256 600k/salt 16 B, formato `pbkdf2-sha256$iter$salt$hash`; username 3–32 `[A-Za-z0-9._-]`; password 12–128; rate-limit login 10/10 min IP + 5/10 min username; sesión usuario opaca 30 min; rutas `POST /users/signup|login|logout` + `GET /users/me` + `GET /users/signup` HTML + `POST /users/erase`; 401 normalizado `managed auth required` por cabecera `X-User-Session`.

## Limitaciones [D/P]

- PG en vivo en CI queda como pendiente de confirmación operativa [P]; lo SQLite/unittest cubierto por suites 299→337 OK [D].
- `docs/PRIVACY.md` pendiente del editor autorizado [P]; este plan no lo reescribe.
- Fase 6 y gates de deploy pendientes (Tasks 6–7) [P].

## Self-review contra spec §§3–11 [C, 2026-10-26]

- §3 modelo/adopción → Tasks 1–2, 6 (Steps 3–4).
- §4 endpoints/auth doble/auditoría → Task 2.
- §5 plugin/gate/memorias/erase separado/Independent → Tasks 3, 5.
- §6 privacidad/erase cascada → Tasks 5, 7.
- §7 sin email → Global Constraints + decisión 1.
- §8 TDD/003/E2E/backup → Global Constraints + Tasks 1–2, 6.
- §9 decisiones → sección Decisiones.
- §10 fuera de alcance (OAuth-login, 2FA/TOTP activo, magic-link, Page FB, T-059) → respetado: reservas a nulo sin código que las active.
- §11 pendientes → Tasks 6–7 + Limitaciones.
- Barrido de marcadores de relleno: este plan no contiene ninguno; todo step trae su contenido (comando o código real).
