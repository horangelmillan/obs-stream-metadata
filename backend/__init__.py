"""obs-stream-metadata — backend centralizado (T-043 foundation, ADR-009).

Fundación stdlib-only (sin dependencias): Shared Kernel + ports + HTTP/API base.
Alcance T-043: health/readiness/version, modelo de errores, configuración,
SecretStore/TokenStore/SessionStore como abstracciones, adapters como estructura
(YouTube/Kick sin OAuth real; Twitch reservado/directo). Sin llamadas a proveedores.

Contratos estables para T-044+:
  backend.app.create_app(...) -> BackendApp
  GET /health -> 200 {"status": "ok", ...}            (liveness, sin auth)
  GET /ready  -> 200/503 {"ready": bool, ...}         (readiness, sin auth)
  GET /version -> 200 {"name", "version", "api"}
Errores: {"error": {"code", "message", "requestId"}} — nunca secretos ni trazas.
"""
