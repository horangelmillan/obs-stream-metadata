# obs-stream-metadata

[![CI](https://github.com/horangelmillan/obs-stream-metadata/actions/workflows/ci-phase0.yml/badge.svg?branch=master)](https://github.com/horangelmillan/obs-stream-metadata/actions/workflows/ci-phase0.yml)

Plugin nativo de **OBS Studio** (C++ / Qt6) + backend Python para gestionar
**título y descripción de tus streams** en **Twitch, YouTube y Kick**
desde un panel integrado en OBS. Sin multistream, sin chat, sin rarezas:
solo los metadatos, aplicados por plataforma, con resultado independiente
para cada una.

## Características

- Panel (dock) dentro de OBS: título + descripción, botón Aplicar.
- Conexión OAuth por proveedor con `Conectado como: <usuario/canal>`.
- Aplicación por plataforma con resultado independiente (si falla Twitch,
  YouTube y Kick igual se actualizan).
- Dos modos de conexión: **Independiente** (gratis, con tus propias apps
  de desarrollador) y **Administrado** (vía backend del proyecto).
- Manejo honesto de errores: 401/403/404/429/5xx, token expirado,
  scopes insuficientes, broadcast no encontrado.
- Seguridad seria: secretos cifrados (DPAPI en local, Fernet en backend),
  nada sensible en logs/repo, revocación real al desconectar.
- Borrado total de datos (`Borrar mis datos`) + purga programada diaria.

## Estado del proyecto

MVP **cerrado y verificado** (backend 218 tests OK, selfcheck OK, CI verde).

| Fase | Estado |
|---|---|
| MVP (dock + OAuth + Apply + hardening) | Hecha |
| F-C1 cifrado de tokens en reposo | Hecha |
| F-C2 borrado real + privacidad operativa | Hecha |
| F-C4 operativa mínima (purga, backups, alertas) | Hecha |
| F-C3 firma del instalador | **Aparcada** (sin financiación para el certificado) |
| T-062 (Apply Managed Twitch) | Hecha (Twitch Managed soportado) |
| T-066 (firma, futuro) | Pendiente |
| T-074 (outage Managed 20–22/09: fuga del pool + errores honestos) | Hecha (prod rev `00028-zml`, `commercial` re-empaquetado) |
| Facebook Live | Solo PoC (FB-1 offline + sondas vivas hechas; FB-2/3/4 pendientes, no es producto aún) |

Detalle vivo: [`docs/STATE.md`](docs/STATE.md). Tareas: [`docs/BACKLOG.md`](docs/BACKLOG.md).

## Instalación (usuarios)

**Requisitos:** Windows 10/11 x64 + OBS Studio **≥ 30.0** (desarrollado
contra 32.2.2).

1. Descarga el instalador `obs-stream-metadata-0.1.0-windows-x64.exe`.
2. Cierra OBS, ejecuta el instalador (requiere admin) y abre OBS.
3. Verás el panel en *Tools / Docks → Stream Metadata*.
4. Elige modo Independiente o Administrado, conecta tus cuentas y pulsa
   Aplicar.

> **Aviso conocido:** el instalador **no está firmado** (F-C3 aparcada),
> así que Windows SmartScreen mostrará "Unknown Publisher". Es esperado:
> acepta el aviso, no sigas trucos raros de internet.

## Uso básico

```text
1. Conectar  →  navegador del sistema  →  login + consentimiento
2. Estado    →  "Conectado como: <usuario/canal>" (jamás verás tokens)
3. Escribir  →  Título (+ Descripción, solo YouTube)
4. Aplicar   →  ✓/✗ por plataforma, sin todo-o-nada
```

**Restricción real (no es un bug):** Twitch y Kick **no tienen**
descripción de stream equivalente a YouTube. La UI lo indica:
Título ✓ en las tres; Descripción ✓ solo en YouTube.

## Modos y plataformas

| Proveedor | Independiente | Administrado | Notas |
|---|---|---|---|
| YouTube | Soportado | Soportado | scope `youtube.force-ssl`; requiere elegir `liveBroadcast` |
| Kick | Soportado | Soportado | scopes `channel:write channel:read`; redirect exacto |
| Twitch | Soportado | Soportado | Independiente: Device Flow sin secret (Client ID vía build); Administrado: auth-code vía backend |
| Facebook Live | No | No | Solo PoC investigativa (`poc/f0fb/`); sin producto ni fechas |

- **Independiente:** tus credenciales viven solo en tu equipo (cifradas
  con DPAPI), jamás salen al backend.
- **Administrado:** OAuth vía backend en Cloud Run + PostgreSQL (Neon);
  el plugin nunca ve client secrets ni tokens.
- Desconectar borra en local + backend e intenta revocar en el proveedor.
- `Borrar mis datos (Managed)` borra tutto lo tuyo del backend de una vez.

## Seguridad y privacidad

Resumen (detalle en [`docs/SECURITY.md`](docs/SECURITY.md) y
[`docs/PRIVACY.md`](docs/PRIVACY.md)):

- OAuth en navegador del sistema con PKCE/state de un solo uso; nada en WebViews.
- Tokens cifrados en reposo (Fernet + DPAPI); sesiones cortas HMAC (30 min).
- Logs redactados: solo plataformas, códigos y longitudes. Cero secretos.
- Retención: sesiones ≤ ~25 h, transacciones ≤ ~24 h (purga diaria);
  conexiones hasta Desconectar/Borrar.
- Contacto de privacidad: `horangelmillan@gmail.com`.
- Sin certificaciones de cumplimiento (no GDPR/SOC2/ISO claims).

## Para desarrolladores

### Requisitos (Windows)

VS2022 + MSVC/ATL + SDK 10.0.22621 + CMake ≥ 3.28 + Git, target
**OBS 32.2.2**, Qt6 vía obs-deps del template (`buildspec.json`).
Detalle validado: [`docs/PROJECT_CONTEXT.md`](docs/PROJECT_CONTEXT.md).

### Compilar el plugin

```powershell
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
.\build_x64\RelWithDebInfo\metadata-selfcheck.exe   # debe terminar en SELFCHECK OK
```

### Backend local

```powershell
python -m backend.app                 # 127.0.0.1:8080, in-memory sin DATABASE_URL
python -m unittest discover -s backend/tests   # 218 OK (skips PG sin servidor por diseño)
```

Con PostgreSQL local, los tests PG corren de verdad con
`STREAM_META_TEST_DATABASE_URL`. Producción = Cloud Run + Neon con
Secret Manager; contrato y procedimientos en
[`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md).

### Estructura

```text
src/            plugin Qt6 (dock, OAuth Independent, storage DPAPI)
backend/        servicio Python (auth, OAuth Managed, metadata, purga /ops/purge)
tools/          scripts de operador (backfill F-C1, purga F-C4, runners T-030)
ops/monitoring/ políticas de alerta + runbook (F-C4)
docs/           harness: STATE, BACKLOG, ARCHITECTURE-BACKEND, DEPLOYMENT,
                SECURITY, PRIVACY, FINDINGS, DECISIONS/ (ADRs), superpowers/plans/
poc/            evidencia histórica T-030 (no tocar)
```

### Cómo contribuir

1. Lee [`docs/STATE.md`](docs/STATE.md), [`AGENTS.md`](AGENTS.md) y
   [`docs/BACKLOG.md`](docs/BACKLOG.md) (la tabla manda; existe `T-###` para todo).
2. Rama `feat/...` o `docs/...` desde `master`, commits en español.
3. PR con el template de [`.github/pull_request_template.md`](.github/pull_request_template.md):
   referencia su `T-###`, evidencia real (comandos + salida), CI verde,
   squash al mergear. Sin atajos destructivos (`docs/GIT.md`).

Mapa de decisiones: [`docs/DECISIONS/`](docs/DECISIONS/) (ADRs).
Puerta histórica del MVP: `AGENTS.md` §50.

## Enlaces oficiales

- OBS: [developer guide](https://obsproject.com/kb/developer-guide) ·
  [plugins](https://docs.obsproject.com/plugins) ·
  [frontend API](https://docs.obsproject.com/reference-frontend-api) ·
  [template](https://github.com/obsproject/obs-plugintemplate)
- Twitch: [API](https://dev.twitch.tv/docs/api/reference/) ·
  [OAuth](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth)
- YouTube: [liveBroadcasts.update](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/update) ·
  [installed-apps OAuth](https://developers.google.com/youtube/v3/guides/auth/installed-apps)
- Kick: [dev portal](https://dev.kick.com) · [docs](https://docs.kick.com)

## Contacto

Dudas y reportes sensibles (no uses issues públicos para datos
sensibles): `horangelmillan@gmail.com`.

## Licencia

Sin licencia publicada aún (pendiente de decisión, ver T-059).
