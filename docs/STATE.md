# STATE — estado actual del proyecto

> Resumen breve reconciliado con el checkout y `docs/BACKLOG.md`
> (canónico: si discrepan, manda BACKLOG). Actualizar en cada tarea
> terminada. Convenciones: [C] comprobado en este checkout ahora mismo
> · [D] documentado (E2E/sesión previa citada, no re-ejecutado aquí)
> · [P] pendiente.

- **Checkout [C]:** rama `master` (`master == origin/master == c6b5b26`,
  "chore: fijar permisos del subagente GCP (#58)"). Baseline al inicio
  de esta tarea documental: árbol limpio salvo `staging/` preexistente
  sin seguimiento (no modificado). Estado actual: mismo HEAD + 8
  archivos documentales modificados sin commitear (AGENTS.md, README.md,
  STATE.md, PROJECT_CONTEXT.md, DEPLOYMENT.md, VALIDATION.md, TOOLS.md,
  WORKFLOW.md); `staging/` preexistente sin seguimiento, no modificado;
  sin commit/push en esta tarea.
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
  facebook` y URL nueva
  `https://obs-stream-metadata-service-mdurv2xfsa-ul.a.run.app`
  [D: `.opencode/agents/gcp-stream-metadata.md`, verificado 2026-10-24
  por lectura]. Drift conocido: `DEPLOYMENT.md`, `tools/deploy.ps1` y el
  job de purga aún referencian la URL anterior
  `...-364043334054.us-east5.run.app`; no asumir que sigue válida [D].
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
  alcance vigente (`AGENTS.md` §23/§51); resolver alcance antes de cambios.
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
- **Rama base:** `master` (ADR-004; CI anclado a `master`).
- **Repo remoto:** `horangelmillan/obs-stream-metadata` (público,
  GitHub-flow con PRs).
- **Restricción vigente:** `poc/` es evidencia histórica + regresión
  (no tocar salvo corrección justificada); el producto vive en `src/`
  + `backend/`.
- **Última actualización:** 2026-10-25 — reforma harness Fase 1
  documental (F-088); sin cambios de código, sin operaciones externas.
  Anterior (2026-10-24): flujo objetivo Facebook Live API documentado
  como propuesta pendiente (`docs/FACEBOOK-LIVE-API-FLOW.md`, T-077).
