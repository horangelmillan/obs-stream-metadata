# DEPLOYMENT — Cloud Run + Neon PostgreSQL (T-055, ADR-014)

Contrato entre código y operador. Sin valores reales (los aporta el
operador). Principio: **Neon es el proveedor inicial; PostgreSQL es el
contrato** (§3 T-055). Nada aquí depende de APIs de Neon.

## Estado de despliegue (T-056, 2026-09-10; rev-00002 en T-057)

Clasificación por registro (IMPLEMENTADO / CONFIGURADO / VERIFICADO /
PENDIENTE / FALLIDO). Nada de esta sección afirma "production deployed".

- IMPLEMENTADO: backend portable PG + migrations + Dockerfile + gates
  prod + binding instalación↔backend (T-053/T-054/T-055 mergeadas).
- IMPLEMENTADO (T-056): bind por entorno — dev `127.0.0.1`, prod
  `0.0.0.0` por defecto, `STREAM_META_BACKEND_HOST` como override.
- CONFIGURADO (GCP, operador): proyecto `obs-stream-metadata`
  (n.º 364043334054, billing Free Trial ~USD 300/90 días hasta aprox.
  10 Dec 2026, sin activación automática); región Cloud Run y Artifact
  Registry `us-east5`; servicio previsto `obs-stream-metadata-service`;
  runtime service account `obs-stream-metadata-backend` (sin roles
  innecesarios, Secret Accessor solo sobre DATABASE_URL, sin Default
  Compute SA); Neon `AWS US East 2 (Ohio)`, branch `production`, DB
  `neondb`, rol `neondb_owner`, pooling ON; DATABASE_URL en Secret
  Manager (versión 1, NO copiarla aquí); scaling min 0 / max 1, 512 MiB,
  1 CPU, timeout 300 s, 2ª generación, request-based billing; acceso
  público a nivel de servicio (la frontera auth es `backend_auth`);
  sin Cloud SQL, sin VPC, cifrado gestionado por Google, sin Binary
  Authorization.
- CONFIGURADO: imagen `us-east5-docker.pkg.dev/obs-stream-metadata/
  obs-stream-metadata/obs-stream-metadata-service:latest` — build PASS,
  push PASS, digest
  `sha256:259661484502da7e858a6faedd74acb31e2bb8e4e71ade600afb1d7f7d7d5e9a`.
- FALLIDO: primer despliegue Cloud Run — el contenedor no escuchó en
  `PORT=8080`. Causa: `serve()` hacía bind en `host` heredado
  (`127.0.0.1`); Cloud Run exige `0.0.0.0`. Corrección T-056 (arriba).
- FALLIDO: revisión `obs-stream-metadata-service-00002-446` — el host
  ya no fue la causa; el wiring productivo exigió
  `STREAM_META_BACKEND_SECRET_DIR` (`ProdstoresError`, fail-fast
  correcto, no workaround). Contrato resuelto en T-057 (sección
  Secret Manager ↔ FileSecretStore): montar volúmenes + `SECRET_DIR`.
- PENDIENTE: repetir despliegue y verificar startup, `/health`,
  `/ready`, `/version`, conectividad PostgreSQL, wiring productivo,
  inyección del secreto, `PUBLIC_URL` y providers de producción.
- PENDIENTE: `STREAM_META_BACKEND_PUBLIC_URL=https://<cloud-run-url>`
  (la URL real se conoce tras un despliegue exitoso; NO inventarla;
  prod exige `https://` por gate T-053).

## Topología

```text
Internet (HTTPS 443)
  → Cloud Run (imagen Dockerfile.backend, stateless, PORT inyectado)
  → DATABASE_URL (postgresql://, SSL)
  → Neon PostgreSQL (Free, proveedor inicial)
  → providers OAuth (HTTPS saliente)
```

El contenedor no persiste nada: ni SQLite, ni ficheros, ni memoria como
fuente de verdad (§11-12 T-055). Toda información persistente vive en
PostgreSQL.

## Variables Cloud Run (producción)

| Variable | Ejemplo (no real) | Notas |
|---|---|---|
| `STREAM_META_BACKEND_ENV` | `production` | Otro valor = fail-fast |
| `STREAM_META_BACKEND_DATABASE_URL` | `postgresql://USER:PASSWORD@HOST/DB?sslmode=require` | LA DATABASE_URL; via Secret Manager, nunca en claro |
| `STREAM_META_BACKEND_PUBLIC_URL` | `https://api.example.com` | HTTPS obligatorio; de aquí derivan `/connect/*/callback` (registrados en las OAuth apps) |
| `STREAM_META_BACKEND_PROVIDERS` | `youtube,kick` | Los que el despliegue habilite |
| `STREAM_META_BACKEND_SECRET_DIR` | `/run/secrets` | Ficheros montados (Google/Kick client IDs+secrets) |
| `STREAM_META_BACKEND_DB_POOL_MAX` | `5` | Conservador: instancias × pool ≤ max_connections Neon Free |
| `STREAM_META_BACKEND_DB_POOL_TIMEOUT_S` | `10` | Espera de conexión del pool |
| `STREAM_META_BACKEND_GLOBAL_LIMIT/WINDOW_S` | `600`/`60` | Puerta global (AllowAll rechazado en prod) |
| `PORT` | (inyectado por Cloud Run) | El backend lo respeta; default 8080 local |

`STREAM_META_BACKEND_TLS_CERTFILE/KEYFILE`: solo si TLS termina en el
backend; con Cloud Run + HTTPS público, terminación en la plataforma.

## Secretos

- `GOOGLE_CLIENT_ID/SECRET`, `KICK_CLIENT_ID/SECRET`: ficheros en
  `$SECRET_DIR` (Cloud Run: Secret Manager montado como volumen o env —
  nunca en la imagen, nunca en el repo, nunca en logs).
- `DATABASE_URL` (con password): Secret Manager; en logs solo forma
  redactada `postgresql://usuario:***@host/db` (redacción verificada).
- OAuth apps PROD (operador): proyecto Google PROD + app Kick PROD
  separados de DEV; redirects = `$PUBLIC_URL/connect/*/callback`.

## Secret Manager ↔ FileSecretStore (T-057)

Contrato verificado: Cloud Run expone secretos como **volúmenes
(tmpfs, stateless-safe)** y `FileSecretStore` los consume como
**ficheros** — mismo mecanismo, cero cambios de código para el mount.
`DATABASE_URL` continúa como variable de entorno (binding ya operativo).

Configuración exacta del operador (nombres ilustrativos; los secretos
viven en Secret Manager, nunca aquí):

```text
--set-secrets=/run/secrets/stream-meta/GOOGLE_CLIENT_ID=GOOGLE_CLIENT_ID:1
--set-secrets=/run/secrets/stream-meta/GOOGLE_CLIENT_SECRET=GOOGLE_CLIENT_SECRET:1
--set-secrets=/run/secrets/stream-meta/KICK_CLIENT_ID=KICK_CLIENT_ID:1
--set-secrets=/run/secrets/stream-meta/KICK_CLIENT_SECRET=KICK_CLIENT_SECRET:1
--set-env-vars=STREAM_META_BACKEND_SECRET_DIR=/run/secrets/stream-meta
--set-env-vars=STREAM_META_BACKEND_DATABASE_URL=(binding existente v1)
--set-env-vars=STREAM_META_BACKEND_PROVIDERS=youtube,kick  (al habilitar)
```

Reglas:

- Un fichero por secreto; el nombre del fichero ES el nombre que lee
  el adapter (`required_secret_names` en cada provider).
- Al arrancar en prod, el wiring verifica los secretos de los providers
  habilitados y falla con los NOMBRES faltantes (nunca valores) antes
  de escuchar — un mount incompleto no se descubre en el primer OAuth.
- Habilitar YouTube/Kick = montar sus 2 ficheros + listar el provider
  en `STREAM_META_BACKEND_PROVIDERS`. Sin mount, el provider no arranca.
- Estado actual: `DATABASE_URL` enlazado (v1); ficheros de providers
  PENDIENTES hasta habilitar OAuth productivo; `PUBLIC_URL` pendiente
  de la URL real del servicio.

## Arranque / salud / parada (Cloud Run)

- Arranque: migrations al inicio (falla antes de escuchar si el esquema
  no aplica); config insegura = fail-fast (stores dev, HTTP, AllowAll,
  sin DATABASE_URL).
- Salud: `/health` (proceso), `/ready` (app+DB), `/version` (`env`).
- Parada: SIGTERM → shutdown graceful (in-flight termina); Cloud Run
  reintenta/reasigna; al no haber estado local, re-arrancar es seguro.
- Logs: stdout (Cloud Logging); solo longitudes/códigos, sin secretos.

## Desarrollo local (§17 T-055)

```powershell
# PostgreSQL local (misma semántica que prod; no requiere Neon):
initdb -D pgdata -U postgres -E UTF8
# editar postgresql.conf: port + listen_addresses 127.0.0.1; arrancar
createdb -h 127.0.0.1 -p PUERTO stream_meta
$env:STREAM_META_BACKEND_DATABASE_URL = 'postgresql://postgres@127.0.0.1:PUERTO/stream_meta'
python -m backend.app                 # dev: in-memory si no hay DATABASE_URL
# Tests PG: $env:STREAM_META_TEST_DATABASE_URL = 'postgresql://postgres@127.0.0.1:PUERTO/postgres'
python -m unittest discover -s backend/tests
```

SQLite queda para tests/dev aislado (marcado `DEVELOPMENT_ONLY`;
producción lo rechaza). Destruir/recrear: `DROP DATABASE` + `CREATE`.

## Backups y exportabilidad (§16 T-055)

- Formato: `pg_dump` (custom o plain) / `pg_restore` — estándar, sin
  herramientas del proveedor.
- Procedimiento reproducible: `pg_dump $DATABASE_URL -Fc -f backup.dump`
  (credenciales por entorno, nunca en repo) → almacenar fuera de la
  instancia → `pg_restore -d NUEVA_DB` → verificar conteos + lectura
  representativa (`/connect/*/status` tras re-apuntar).
- Backup del proveedor (Neon) ≠ backup controlado: el segundo es el que
  permite migrar de proveedor.
- MVP: backup manual antes de cada cambio de esquema; automatizar
  (schedule) al crecer. Probar la restauración, no solo el dump.

## Migración Neon → Cloud SQL (§14-15 T-055, obligatoria documentada)

- **MVP (poco volumen):** crear Cloud SQL PostgreSQL → aplicar las
  mismas migrations del repo → `pg_dump` Neon → `pg_restore` Cloud SQL →
  validar (esquema, tablas, conteos, constraints, índices, lecturas) →
  cambiar `DATABASE_URL` + nueva revisión Cloud Run → health/readiness +
  operaciones reales → rollback = volver a la URL anterior.
- **Crecimiento moderado:** volcado en caliente + ventana de
  mantenimiento comunicada; re-validar y cortar.
- **Grande/activa:** replicación continua (herramienta PG) + cutover con
  downtime mínimo y controlado. Nunca prometer cero absoluto.
- Continuidad: infra nueva lista antes de cortar; copia → validación →
  deployment → cutover → rollback disponible; comunicar solo si hay
  indisponibilidad real. Es operación de infraestructura, no reescritura.

## Costos (§24 T-055, 2026-09: modelo, no precios)

Validación inicial con tiers gratuitos (Cloud Run + Neon Free) dentro de
sus límites: coste ~cero. Al superarlos, el uso factura por consumo y la
arquitectura no cambia. Si conviene, Neon → Cloud SQL es migración de
datos+config (ver arriba). Sin precios hardcodeados en código ni como
garantía (límites y tarifas los fija cada proveedor).

## Checklist operador (antes de declarar prod UP)

- [ ] `/version` → `"env": "production"`; `/ready` 200
- [ ] HTTPS válido extremo a extremo; `PUBLIC_URL` = URL pública real
- [ ] `DATABASE_URL` solo en Secret Manager; redacción verificada en logs
- [ ] Secrets solo en `$SECRET_DIR`; auditoría con patrón CI
- [ ] Migrations aplicadas (`schema_migrations`); backup inicial + prueba
- [ ] OAuth apps PROD con redirects exactos (connect real por proveedor)
- [ ] Pool dimensionado (instancias × max ≤ max_connections)
- [ ] Rate-limit 429 ante abuso; Disconnect borra + revoca
- [ ] Rollback conocido (revisión anterior + URL anterior)

## Dependencias externas (operador)

Proyecto GCP + servicio Cloud Run, proyecto Neon (Free) + `DATABASE_URL`,
dominio+DNS+certificado (gestionado por la plataforma), OAuth apps PROD
+ credenciales, monitoreo/backups gestionados al crecer. Licencias/pagos
(T-055 en BACKLOG como diseño comercial) y observabilidad avanzada fuera
de esta tarea.
