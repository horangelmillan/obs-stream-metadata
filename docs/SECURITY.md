# SECURITY — reglas desde Phase 0

- Nunca en Git/logs/código/docs/commits: `client_secret`, `access_token`, `refresh_token`, `Authorization: Bearer`.
- OAuth lo gestiona el código nativo; jamás en WebView/Browser Source (`AGENTS.md` §16-17).
- `.gitignore` cubre `.env`, `*.key/pem/p12`, `*token*.json`, `credentials.json`, `client_secret*.json`.
- Datos de prueba: ficticios y marcados como tales. Logs con IDs enmascarados.
- Desconexión por proveedor elimina tokens/identidad local; evaluar endpoint de revocación (AGENTS §29).
- Incidente: rotar credencial, `revert` del commit, registrar en FINDINGS.
- PoC T-030: tokens solo en memoria + intercambio manual del operador; `tools/t030_oauth_callback.py` no escribe disco ni logs (muestra el code una vez por consola local para el intercambio inmediato). Sin secret embebido: Twitch usa device flow (F-015); Kick deja abierto el problema del secret de escritorio (F-017, resolver en P5).
- PoC T-030 (vivas 2026-09-08): runners `tools/t030_live_*.py` nunca imprimen tokens/codes/verifiers/secrets (solo longitudes); secret Desktop de Google y secret de Kick solo vía local (`--flag` / env / pregunta oculta), revocados al final de cada PASS. Ni codes ni tokens en chat, capturas, repo, logs ni PR (F-018, F-024). Sesiones con FAIL interrumpen antes del revoke → revocar grants manualmente en la plataforma.
