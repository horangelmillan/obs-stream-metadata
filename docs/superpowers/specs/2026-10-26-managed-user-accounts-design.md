# Cuentas de usuario Managed — diseño aprobado (2026-10-26)

> Transcripción fiel del diseño "Cuentas de usuario Managed" aprobado por el
> usuario en chat el 2026-10-26 [D]. Redacta `harness-steward` bajo excepción
> puntual 2026-10-26 [D] (autorización de la tarea; precedentes `docs/BACKLOG.md`,
> `.gitignore`, `docs/MEDIAMTX-RELAY-FLOW.md`). Este documento es **diseño**,
> no implementación: sin código, sin producto, sin operaciones externas.
> Etiquetas: [C] comprobado en esta sesión · [D] documentado · [I] inferido ·
> [P] pendiente. Fuente canónica de tareas: `docs/BACKLOG.md`; de privacidad
> descriptiva: `docs/PRIVACY.md`; de arquitectura: `docs/ARCHITECTURE-BACKEND.md`.

## 0. Estado y trazabilidad

- Estado: **diseño aprobado por el usuario (2026-10-26)** [D] (chat de
  aprobación; transcript fuera del repo, no duplicado aquí).
- Decisiones registradas en §9 con fecha 2026-10-26 [D].
- Relacionado (no duplicado): `docs/notes/007-portal-auth-managed-suscripciones.md`
  (idea de portal + OAuth de proveedores + suscripción, estado idea) [D, 2026];
  BACKLOG T-059 (futuro, suscripción/licencias) [D]; BACKLOG T-064 / F-C2
  (borrado real + `POST /privacy/erase`) [D]; `docs/PRIVACY.md` §§2–9/14
  (a actualizar tras este diseño, ver §6) [D].
- Lo no fijado en el diseño aprobado queda marcado [P]; nada de este spec
  inventa decisiones de producto.

## 1. Objetivo

Dar identidad de **usuario final** al modo Managed para sostener control de
acceso y, a futuro, planes de pago (T-059), añadiendo una capa de seguridad
sobre el bootstrap anónimo rate-limitado vigente (`backend_auth`, sesiones
HMAC) sin romper el modo Independent [D].

## 2. Enfoque: A elegido; B/C descartados

- **Enfoque A (adoptado)** [D]: **tokens del usuario desde el día uno**.
  Cada cuenta Managed pertenece a un `user` desde su creación; no existen
  cuentas Managed huérfanas tras esta migración.
- **Enfoques B/C (descartados)** [D]: alternativas consideradas en el chat
  de diseño y descartadas por el usuario el 2026-10-26.
  Motivo de descarte [D]: no garantizan tokens del usuario desde el día uno
  (postergan la propiedad usuario→tokens a una migración posterior o la
  dejan opcional), lo que el usuario rechazó. El detalle literal de B/C vive
  en el transcript del chat 2026-10-26 (fuera del repo); este spec no lo
  reconstruye para no inventar [P si se quiere archivar el literal].

## 3. Modelo de datos

- Nueva tabla **`users`** (lógico, tipos/longitudes exactos [P] a fijar en
  implementación TDD) [D]:
  - identificador de usuario (PK);
  - nombre de usuario / login (único);
  - hash de contraseña (algoritmo y parámetros [P]; jamás en claro ni en logs);
  - marcas temporales (`created_at`, `updated_at` [P]);
  - columnas reservadas **sin implementar en v1**: TOTP/`totp_secret` y
    marcadores de OAuth-login (`oauth_provider`, `oauth_subject`) — solo
    reserva de nombre, sin código ni UI [D].
- Propagación de propiedad [D]:
  - `installations.user_id`, `sessions.user_id`, `connections.user_id`;
  - `tokens.owner_user_id` (el token pertenece al usuario, no a la instalación).
- **Adopción de instalaciones anónimas sin reset** [D]: las instalaciones
  existentes (bootstrap anónimo vigente) se adoptan al primer login/signup
  del usuario — se les asigna `user_id` sin regenerar identidad ni borrar
  `backendInstall`, sin downtime y sin obligar a re-bootstrap.
- Backfill de la migración 003: filas pre-existentes reciben su `user_id` /
  `owner_user_id` por adopción en el momento del login/signup; antes de eso
  el esquema lo permite como pendiente de adopción [P detalle de nulabilidad
  a fijar en la migración; ver §8].

## 4. Endpoints

- Nuevos lógicos (rutas HTTP exactas y códigos [P] a fijar en implementación;
  nombres lógicos [D]): **`signup` / `login` / `logout` / `me`** [D].
  - `signup`: alta con usuario + contraseña v1 (sin email, ver §7).
  - `login`: emite sesión de usuario; con **rate-limit específico de login**
    (ventana/umbral [P]) adicional al rate-limit vigente [D].
  - `logout`: revoca la sesión de usuario [D].
  - `me`: devuelve identidad del usuario autenticado (sin secretos) [D].
- **Auth doble en `connect` y `metadata`** [D]: las rutas Managed de conexión
  y de aplicación de metadata exigen **sesión de instalación válida (T-044) +
  sesión de usuario válida**; falta cualquiera de las dos → error de
  autenticación normalizado (sin fuga de cuál falló más allá de lo necesario)
  [P mensaje exacto].
- **Auditoría** [D]: registrar eventos de cuenta (signup/login/logout/erase;
  nivel y retención [P]) solo con identificadores no sensibles, códigos y
  contadores — jamás contraseñas, hashes, tokens ni secretos (hereda
  `RedactingFilter` + leak-guard de `PRIVACY.md` §2.5/§11).

## 5. Plugin

- **Gate login previo a Managed** [D]: el dock exige login de usuario antes
  de cualquier operación Managed (Connect/Apply/erase); sin login, las
  acciones Managed quedan deshabilitadas con mensaje de reconexión/login,
  sin red ni fallback silencioso a Independent.
- **Sesiones en memoria** [D]: la sesión de usuario en el plugin vive solo
  en memoria (nunca en `accounts.json` ni DPAPI); al reiniciar OBS se
  revalida o se pide login de nuevo [P detalle UX a fijar en implementación].
- **Erase de cuenta separado** [D]: el borrado de cuenta de usuario
  (`/users/erase` lógico, nombre exacto [P]) es operación distinta del
  `POST /privacy/erase` vigente (F-C2, borrado por instalación): no se
  fusionan en v1.
- **Independent intacto sin login** [D]: el modo Independent (BYO-app/DPAPI,
  ADR-008/ADR-012) no requiere ni acepta login de usuario; ningún cambio de
  este diseño altera sus flujos, almacenamiento o UX.

## 6. Privacidad y borrado

- `user_id` pasa a ser el **primer identificador de persona** del backend
  Managed [D]; `PRIVACY.md` debe actualizarse en implementación (nuevas filas:
  `users`, `user_id`/`owner_user_id`; §2–§9/§14 y apéndice) — este spec no lo
  reescribe, solo lo ordena como tarea [P].
- **`/users/erase` en cascada preservando F-C2** [D]: el borrado de cuenta
  elimina en cascada usuarios→instalaciones→sesiones→conexiones→tokens no
  compartidos del usuario, con revoke best-effort en proveedores (preserva la
  semántica F-030/F-C2: local primero, revoke después, idempotente, solo
  conteos en respuesta/logs). Semántica de filas compartidas (`tokens`
  referenciados por otra instalación del mismo usuario u otro usuario [P]) se
  fija en implementación sin relajar F-C2.

## 7. Abierto explícito: sin email en v1

- **Sin email en v1** [D]: el diseño aprobado no incluye campo email, ni
  verificación por correo, ni reset de contraseña. Diferido explícito a
  **v1.1** [D]. Consecuencia aceptada: sin recuperación de cuenta en v1
  (documentar en UX; sin soluciones improvisadas en implementación).

## 8. Testing y rollout

- **TDD** [D]: tests primero (rojo→verde) para modelo, endpoints, auth doble,
  rate-limit login, adopción y erase en cascada; sin atajos.
- **Migración 003 con backfill** [D]: migración numerada `003_*` (nombre
  exacto [P]) que crea `users`, añade `user_id`/`owner_user_id` y ejecuta el
  backfill/adopción del §3; ordenada tras las migraciones vigentes, una
  transacción por fichero (patrón `backend/migrations/` + `schema_migrations`,
  `docs/ARCHITECTURE-BACKEND.md` §20).
- **E2E multi-PC** [D]: validación con dos PCs/instalaciones sobre el mismo
  usuario (adopción sin reset + aislamiento por instalación + Apply en ambas).
- **Backup DB** [D]: `pg_dump` antes de aplicar la 003 en cada entorno
  (patrón `docs/DEPLOYMENT.md` §20); rollback = restaurar backup + URL
  anterior, nunca migración hacia atrás improvisada.

## 9. Decisiones del usuario (2026-10-26) [D]

| # | Decisión | Fecha | Fuente |
|---|---|---|---|
| 1 | Contraseña en v1 (`password-v1`, sin email ni 2FA) | 2026-10-26 | chat de aprobación [D] |
| 2 | Tokens del usuario desde el día uno (`tokens-del-usuario`, enfoque A; B/C descartados) | 2026-10-26 | chat de aprobación [D] |
| 3 | Acceso web + adopción (`web+adopción`: login también desde página web sencilla; instalaciones anónimas se adoptan sin reset) | 2026-10-26 | chat de aprobación [D] |

## 10. Fuera de alcance (v1)

OAuth-login (login con Google/Twitch/Kick/Facebook), 2FA/TOTP activo,
magic-link, Page de Facebook y suscripción/pagos (T-059) [D]. Los campos
reservados del §3 no habilitan ninguno de estos; activarlos exige spec y
gate separados.

## 11. Pendientes explícitos [P]

Rutas/códigos HTTP exactos, algoritmo de hash + parámetros, ventanas de
rate-limit login, nulabilidad transitoria del backfill, semántica de tokens
compartidos en erase, detalle UX del gate login y retención de auditoría.
Nada de lo [P] bloquea este diseño; todo lo [P] bloquea la implementación
hasta fijarse con evidencia (TDD + E2E + backup).

## 12. Self-review del steward (inline, 2026-10-25) [C]

- Placeholders: barrido `TBD/TODO/por definir` — solo quedan marcas [P]
  explícitas del §11, sin contenido inventado [C].
- Contradicciones: Independent sin login (§5) vs auth doble solo en Managed
  (§4); erase de cuenta separado de F-C2 (§§5–6); sin email (§7) vs
  OAuth-login fuera de alcance (§10) — coherentes [C].
- Alcance: este archivo es diseño (transcripción autorizada); no toca
  `src/`, `backend/`, `tools/`, secretos ni infra [C].
- Ambigüedades corregidas: B/C no reconstruidos (remiten al transcript);
  rutas/tipos/parámetros marcados [P] en vez de fijados aquí [C].
