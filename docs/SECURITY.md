# SECURITY — reglas desde Phase 0

- Nunca en Git/logs/código/docs/commits: `client_secret`, `access_token`, `refresh_token`, `Authorization: Bearer`.
- OAuth lo gestiona el código nativo; jamás en WebView/Browser Source (`AGENTS.md` §16-17).
- `.gitignore` cubre `.env`, `*.key/pem/p12`, `*token*.json`, `credentials.json`, `client_secret*.json`.
- Datos de prueba: ficticios y marcados como tales. Logs con IDs enmascarados.
- Desconexión por proveedor elimina tokens/identidad local; evaluar endpoint de revocación (AGENTS §29).
- Incidente: rotar credencial, `revert` del commit, registrar en FINDINGS.
