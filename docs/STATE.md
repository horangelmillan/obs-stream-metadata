# STATE — estado actual del proyecto

> Resumen breve reconciliado con el checkout y `docs/BACKLOG.md`
> (canónico: si discrepan, manda BACKLOG). Actualizar en cada tarea
> terminada. Convenciones: [C] comprobado en este checkout ahora mismo
> · [D] documentado (E2E/sesión previa citada, no re-ejecutado aquí)
> · [P] pendiente.

- **Checkout [C]:** rama `master` (HEAD `174b144`, merge PR #71
  `feat/sesion-persistente-t086`). Árbol limpio al inicio de esta
  tarea documental (`git status --porcelain` vacío); sin commit/push
  en esta tarea. `.env`/`dev.env` ignorados (`.gitignore:61/63`);
  sin lectura de secretos ni de `staging/` aquí.
- **Fase actual:** sin ciclo de desarrollo abierto. BACKLOG reconciliado
  [C contra `docs/BACKLOG.md`]: todo hecha salvo `T-002` (en-progreso,
  informe Phase 0, no bloquea), `T-059`/`T-066` (futuro: licencias/firma)
  y la fila `T-058` duplicada histórica (+ `T-042` superseded).
  Últimas: T-075 (PR #54) y T-076 (PR #55) hecha el 2026-10-24 [D].
- **Qué hicimos (post T-033):** T-062 Apply Managed Twitch (E2E de facto
  vía T-063) [D]; F-C1/F-C2/F-C4 (cifrado, borrado, purga+backups+
  alertas) [D]; T-067 `dist/` + `tools/package.ps1` [D]; T-068 Apply
  Managed Kick + `tools/deploy.ps1` [D]; T-069–T-073 Facebook perfil
  (Independent T-071, Managed T-072, cierre T-073 con PR #52) [D];
  T-074 outage 20–22/09 (fuga del pool + 401/500 honestos, rev
  `00028-zml`) [D]; T-075 auto-reconnect cómodo (PR #54) [D]; T-076
  Facebook gris + YouTube default listo (PR #55, E2E operador, rev
  `00036-cjj`) [D].
- **Baselines en este checkout [C, conteo estático sin ejecutar]:**
  backend 250 métodos `def test_*` en `backend/tests/` (coherente con el
  "250 OK" documentado en T-075/T-076); selfcheck 180 invocaciones
  `CHECK(` en `src/metadata_selfcheck.cpp` (+1 línea `#define`; último
  resultado documentado: SELFCHECK OK, T-073). No se ejecutaron pruebas
  en esta sesión documental.
- **Capacidades vigentes [C código + D E2E]:** un solo plugin, dos modos
  (Independiente BYO-app / Administrado vía backend). Twitch, YouTube,
  Kick y Facebook-perfil soportados en ambos modos
  (`managedSupported()==true` en `src/metadata_dock.cpp:928-932`;
  adapters + tests en `backend/`); descripción de stream solo en
  YouTube y Facebook-perfil (matriz `AGENTS.md` §3). Integración de
  Pages de Facebook: diferida, sin fechas. Estado de la app de Meta y
  requisitos de acceso para las pruebas del operador: pendientes de
  confirmar (sin afirmar Live, App Review ni acciones iniciadas en
  Meta).
- **Distinción obligatoria (AGENTS.md §55.8):** app de Meta en modo
  Development y sin App Review ⇒ solo sirve a usuarios con rol en la
  app (operador); NO es desbloqueo para terceros [D: F-072/F-079,
  DEPLOYMENT]. Hecho independiente del despliegue del backend en Cloud
  Run [D: subagente GCP 2026-10-24].
- **Cloud Run (estado operativo; detalle en DEPLOYMENT.md):**
  documentado rev `00036-cjj` con `PROVIDERS=youtube,kick,twitch,
  facebook`. URL canónica 2026-10-26 [D, confirmada por el usuario en
  chat; coincide con `FB_URL_APP` en `.env` y con `PUBLIC_URL` en
  Cloud Run, evidenciado por backend-integrations ese día sin citar
  valores]:
  `https://obs-stream-metadata-service-364043334054.us-east5.run.app`.
  La hash
  `https://obs-stream-metadata-service-mdurv2xfsa-ul.a.run.app` queda
  como alterna no canónica [D histórico 2026-10-24]. `tools/deploy.ps1:58`
  construye la canónica: NO necesita cambio [C, leído en esta sesión].
  Sin re-verificación en vivo en esta sesión [P].
- **Discrepancias residuales conocidas [P, requieren confirmación]:**
  1. `ARCHITECTURE-BACKEND.md` §18 fila Twitch Managed ("pendiente",
  "direct only") desactualizada frente al código (adapter + Apply +
  `managedSupported()==true`) y BACKLOG T-062 (precedente F-064; ARCH
  fuera del alcance de esta tarea documental). 2. `PRIVACY.md` declara
  "Twitch Managed is unsupported" (desactualizado; mismo origen).
  3. Datos vivos de producción (URL, revisión al 100 %, E2E) citados
  como documentados, no re-verificados aquí.
- **Plan del operador pendiente [P, intención comunicada por el usuario
  el 2026-10-24]:** avanzar durante octubre de 2026 hacia pruebas de
  Facebook con usuarios reales y streams reales. No se afirma que la app
  de Meta esté en Live, que App Review esté aprobado ni qué requisitos
  específicos aplican (sin evidencia). Tarea separada del despliegue del
  backend en Cloud Run (ver DEPLOYMENT.md); requiere confirmación y
  alcance explícito antes de ejecutar nada.
- **Facebook Live API [P]:** el operador quiere validar el flujo de
  LiveVideo creado por API con título, descripción y audiencia Público,
  ingestión de OBS/Aitum y publicación final. El proceso propuesto está en
  `docs/FACEBOOK-LIVE-API-FLOW.md`, T-077. F-067 ya documenta create/update/
  read-back de metadatos, pero la emisión real vía ingestión y publicación
  visible aún requieren validación. La integración Aitum/RTMP está fuera del
  alcance vigente (`AGENTS.md` §23/§51); resolver alcance antes de cambios. E2E lab 2026-10-26 [D]:
  Connect PKCE Test App PASS; create UNPUBLISHED EVERYONE ×5 PASS
  (IDs `28703591109303258`, `28703717372623965`, `28703720135957022`,
  `28703757945953241`, `28703813379281031`); read-back título/desc/
  status idénticos PASS; update+read-back PASS; ingesta manual Aitum →
  GoLive 200 → LIVE + DIRECTO visible en perfil PASS; End →
  LIVE_STOPPED + DELETE ×2 (100/33) PASS; bug lab Status-comas
  corregido (solo lab). Veredictos negativos: persistencia NO url única
  (F-092); privacy ilegible en GET (Meta 400) + badge "Solo yo" pese a
  EVERYONE 200 (F-093); localhost bloqueado en app Live con Enforce
  HTTPS, Test App como vía (F-094). Pendientes [P]: audiencia en
  producción, port al plugin, resolución del conflicto de alcance
  §§23/51 antes de implementar producto. BACKLOG canónico: T-077 sigue
  en investigación, sin cambio de estado. Detalle en VALIDATION.md.
- **Relay local MediaMTX [D E2E lab 2026-10-26, T-078 en investigación]:** propuesta validada en lab (`docs/MEDIAMTX-RELAY-FLOW.md` como guía; T-078): MediaMTX v1.21.3 local con forward RTMPS en caliente (PATCH sin reinicio, formato base+`#`+clave) → 2 emisiones LIVE+DIRECTO con movimiento sin tocar Aitum entre ellas; responde a F-092 (sin URL única de ingesta) con URL fija local + re-fijado del forward. Nuevo punto de partida arquitectónico pendiente de port al plugin y de resolución del conflicto de alcance §§23/51. Pendientes [P]: métricas latencia/CPU/recovery (F-099), audiencia en producción (sigue "Solo yo" pese a EVERYONE 200, como F-093), rate-limit a re-handshakes (~3 min, F-097). Detalle en VALIDATION.md T-078 y F-096–F-099. BACKLOG canónico: T-078 sigue en investigación (aceptación parcialmente cubierta), T-077 intacta.
- **Reforma harness en curso [C, 2026-10-25, Fase 1 documental]:** apertura
  mínima-privilegio + validación por stack + reparto steward/BACKLOG y
  ejecutor lab (F-088). Cambios solo documentales/permisos, sin código:
  `harness-steward` único escritor de `docs/BACKLOG.md` + dueño de
  `.gitignore`; `release-tooling` en `tools/**` (sin ejecución);
  `backend-integrations` dueño ejecutor de `lab/**` (aún no existe en el
  checkout [C]); `obs-plugin` valida con `cmake*/ctest*/msbuild*`;
  `AGENTS.md` §56 con tabla de propiedad; TOOLS/WORKFLOW reconciliados.
  Sin cambios de estados BACKLOG en esta tarea. Próximo gate: Fase 2 lab
  por `backend-integrations` (incl. entrada `node_modules` en
  `.gitignore` y comprobación `browser: ask` con `opencode debug
  agents`).
- **Cuentas Managed v1 Fases 1–5 cerradas (2026-10-26) [D/C]:** commit
  `0f015a3` (`feat(managed-users-v1): users PBKDF2 + 003 + endpoints +
  auth doble + gate plugin + erase (Fases 1-5)`, en el historial de este
  checkout [C: listado en `git log --oneline`]) implementa: modelo
  `users` PBKDF2-SHA256 600k + migración `003_users.sql` (Fase 1),
  endpoints `POST /users/signup|login|logout` + `GET /users/me` + auth
  doble Managed con 401 normalizado (Fase 2), gate login en el plugin
  con sesión solo-memoria (Fase 3), selfcheck f4/f5 (Fase 4) y
  `POST /users/erase` en cascada separado de F-C2 (Fase 5). Evidencia
  [D, no re-ejecutada aquí]: suites 299→337 OK + selfcheck f4/f5 OK +
  secret-scan limpio (mensaje de `0f015a3` + plan
  `docs/superpowers/plans/2026-10-26-managed-user-accounts.md`).
  Canónico en BACKLOG T-079–T-084; acta en VALIDATION.md.
  `docs/PRIVACY.md` actualizado con el inventario Managed v1
  (descriptivo; el código manda). Deploy y smoke cerrados abajo; pendiente [P] solo Fase 6:
  backup/rollback (T-084, operador). Cierre 2026-10-26 [D]:
  tag `rev-managed-users-v1` hacia rev `obs-stream-metadata-service-
  00039-7jl` al 100% (`/health` ok, `/version` env=production,
  `/ready` true; reporte release-tooling + dictamen gcp en vivo);
  migracion 003 en prod (`schema_migrations` 001/002/003, consulta
  MCP Neon read-only a rama `production`); snapshot manual
  `snap-fancy-lab-a5r16mex` (2026-10-26T02:11:27Z; pg_dump opcional);
  smoke prod con usuario sintetico desechable PASS con limpieza
  (signup 201, login 200, me 200, status sin sesion 401, logout 200,
  me 401, erase 200, me 401; quedan 3 instalaciones huerfanas vacias
  de depuracion, sin datos: job diario o purga operativa [P]).
  Detalle en VALIDATION.md. Pendiente [P]: E2E operador con cuentas
  reales (OAuth por plataforma + Apply real + dos PCs; T-084 sigue
  pendiente, BACKLOG canonico).
- **Regla logout/login Managed (T-085, 2026-10-26) [D]:** logout explícito = desconexión total Managed de la PC; re-login mismo usuario conserva; otro usuario entra limpio; reinicio sin logout conserva. Pendiente [P]: desplegar backend + E2E operador.
- **Sesión Managed persistente (T-086, 2026-10-26) [D, decisión del usuario; sustituye spec §5 memoria-solo]:** la sesión de usuario Managed persiste cifrada en DPAPI (`managedUser`: token+expiry+username, fail-closed ante mismatch de `backendBaseUrl`) hasta logout explícito/401/expiración; al abrir OBS con sesión válida → directo al panel; login solo si venció o hubo logout. Implementado por obs-plugin + routing ux-ui (cambios sin commitear en este checkout [C]); build+selfcheck verdes según sesión implementadora (`startup-*`/`muser-*`/`card-*`/`f6-f8` PASS, DLL OK) [D]. Pendientes [P]: integrar (commit/PR), recompilar instalador, E2E operador con capturas.
- **E2E operador single-PC Managed (2026-10-26) [D, chat del operador; autorización: confirmó en prod los 3 puntos con "OK OK OK"]:** 1. reapertura con sesión válida → directo al panel (sesión persistente T-086 OK en vivo); 2. logout → pantalla entrar + plataformas en gris neutro, sin rojos (T-086 OK en vivo); 3. logout desconectó las 4 plataformas (regla T-085 funcionando en vivo). Pendiente [P]: T-084 multi-PC (requiere 2 máquinas) + OAuth real con Apply de título real del operador. Detalle en VALIDATION.md. Sin cambios de estado BACKLOG en esta tarea (fuera de alcance).
- **Rama base:** `master` (ADR-004; CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público,
  GitHub-flow con PRs).
- **Restricción vigente:** `poc/` es evidencia histórica + regresión
  (no tocar salvo corrección justificada); el producto vive en `src/`
  + `backend/`.
- **Última actualización:** 2026-10-26 — E2E operador single-PC
  Managed PASS (sesión persistente → directo al panel + logout a
  entrar en gris neutro + desconexión total de las 4 plataformas;
  "OK OK OK" en prod [D]; T-084 multi-PC + OAuth real con Apply
  pendientes [P]); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): T-086 hecha (sesión Managed persistente DPAPI + routing arranque + estados gris/rojo tarjeta; sustituye spec §5 por decisión del usuario [D]; build+selfcheck verdes [D]; pendientes [P]: integrar, recompilar instalador, E2E operador con capturas); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): T-085 hecha (regla "logout limpia, entrar limpio" implementada y verificada según autorización [D]; 8 tests nuevos + suite 345 OK; pendiente [P]: desplegar backend + E2E operador); sin commit/push en esta tarea documental.
  Anterior (2026-10-26): loop deploy+verificación
  Managed v1 cerrado (rev `00039-7jl` al 100%, migración 003 en prod,
  snapshot `snap-fancy-lab-a5r16mex`, smoke sintético PASS con limpieza;
  T-084 E2E operador pendiente); sin cambios de código, sin operaciones
  externas en esta sesión documental.
  Anterior (2026-10-26): E2E relay T-078 documentado
  (forward + caliente PASS, métricas parciales; sigue en investigación,
  T-077 intacta); sin cambios de código, sin operaciones externas.
  Anterior (2026-10-25): reforma harness Fase 1 documental (F-088).
  Anterior (2026-10-24): flujo objetivo Facebook Live API documentado
  como propuesta pendiente (`docs/FACEBOOK-LIVE-API-FLOW.md`, T-077).
