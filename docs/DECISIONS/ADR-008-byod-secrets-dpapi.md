# ADR-008 — Credenciales BYO-app + custodia DPAPI (P5)

- **Fecha:** 2026-09-09 · **Tarea:** T-032 · **Estado:** decidido, implementado, pendiente validación viva del operador.
- **Nota de estado (2026-09-09, T-038/ADR-009):** ADR-008 describe la arquitectura BYO-app de P5/T-032, vigente como **modo operador/debug + hardening local** (DPAPI, revoke, backoff). La arquitectura de distribución la define ADR-009 (backend YouTube+Kick, Twitch directo). Ver `docs/ARCHITECTURE-BACKEND.md`.
- **Problema (F-017/F-019):** YouTube exige `client_secret` incluso en clientes Desktop y Kick exige secret aun con PKCE. Un secret compartido embebido en el binario no es custodiable (repo público) y las env vars de T-031 no son UX final ni sobreviven reinicios.
- **Elección:**
  1. **BYO-app (bring-your-own-app):** el operador registra una app por plataforma (Twitch consola dev, Google Cloud cliente Desktop + YouTube Data API, Kick portal dev con redirect `http://localhost:3000/cb`) y escribe IDs/secrets **una vez** en el dock. Secret embebido compartido: PROHIBIDO.
  2. **Custodia local:** memoria de proceso + fichero `accounts.json` en el config dir del módulo (`obs_module_config_path`, `%APPDATA%\obs-studio\plugin_config\obs-stream-metadata\` en Windows) con los campos sensibles (tokens, IDs, secrets) cifrados por **DPAPI CurrentUser** (`CryptProtectData`, `CRYPTPROTECT_UI_FORBIDDEN`). Etiquetas no sensibles (display, broadcaster) en claro. Plaintext de secretos: prohibido.
  3. **Revoke real:** Disconnect revoca access+refresh en el proveedor (mismo wire format que los runners P3) y luego borra el registro local; best-effort en segundo plano, solo códigos HTTP en logs.
  4. **Backoff acotado:** 429/5xx en Apply reintentan como máximo 2 veces (2 s, 4 s), conservando el presupuesto de refresh-único; sin loops. El refresco de lista de broadcasts informa sin reintentar (acción iniciada por el usuario).
  5. **Fin de env vars:** `STREAM_META_*` dejan de leerse; el dock es la única entrada.
- **Consecuencias:**
  - El operador crea 3 apps una vez (procedimiento en `TROUBLESHOOTING.md` T-032). Sin backend ni intermediarios.
  - Fuera de Windows no hay persistencia (solo memoria); el MVP declara Windows como plataforma objetivo.
  - La matriz T1–T20 y §39.5-9 (revocación/reconexión) las valida el operador en P6.
