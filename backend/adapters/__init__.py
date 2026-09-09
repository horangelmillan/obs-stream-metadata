"""Adapters: un módulo por proveedor contra el port OAuthProvider.

T-043: solo estructura + metadatos declarativos (scopes, capabilities).
OAuth real y llamadas a proveedor: T-045 (YouTube), T-046 (Kick).
Twitch permanece directo desde el plugin (F-015/F-033); su adapter backend
queda reservado para paridad futura sin rediseño del kernel.
"""
from backend.adapters import kick, twitch, youtube  # noqa: F401
