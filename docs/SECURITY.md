# SECURITY — reglas desde Phase 0

- Nunca en Git/logs/código/docs/commits: `client_secret`, `access_token`, `refresh_token`, `Authorization: Bearer`.
- OAuth lo gestiona el código nativo; jamás en WebView/Browser Source (`AGENTS.md` §16-17).
- `.gitignore` cubre `.env`, `*.key/pem/p12`, `*token*.json`, `credentials.json`, `client_secret*.json`.
- Datos de prueba: ficticios y marcados como tales. Logs con IDs enmascarados.
- Desconexión por proveedor elimina tokens/identidad local; evaluar endpoint de revocación (AGENTS §29).
- Incidente: rotar credencial, `revert` del commit, registrar en FINDINGS.
- PoC T-030: tokens solo en memoria + intercambio manual del operador; `tools/t030_oauth_callback.py` no escribe disco ni logs (muestra el code una vez por consola local para el intercambio inmediato). Sin secret embebido: Twitch usa device flow (F-015); Kick deja abierto el problema del secret de escritorio (F-017, resolver en P5).
