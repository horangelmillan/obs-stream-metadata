# 003 — Rate limits con mensajes útiles (tiempo de espera + intentos)

## Contexto

F-C2, 2026-09-19: probando el borrado total se agotó `bootstrap 5/h/IP`
y el dock mostraba "rate limited. Wait and retry." sin tiempo ni intentos
restantes. Idea del operador.

## Problema observado

El usuario no sabe **cuánto esperar** ni **cuántos intentos le quedan**.
El backend responde 429 genérico y el dock muestra texto fijo.

## Propuesta

- **Backend propio (control total):** extender el cuerpo 429 con
  `retry_after_s` (+ header `Retry-After`); el dock muestra cuenta atrás.
  Cubre `bootstrap 5/h-IP`, `auth 30/min-instalación`, `global 600/min`.
- **Proveedores (solo con datos reales):** Twitch devuelve
  `Ratelimit-Limit/Remaining/Reset` en Helix; YouTube 429/
  `rateLimitExceeded` habla de cuota diaria (10.000 u/día), no de
  segundos; Kick pendiente de documentar (`docs.kick.com`). Nunca
  inventar cifras.
- Cautela anti-abuso: granularidad gruesa del `Retry-After`
  (p. ej. redondeo a 30 s) para no filtrar la defensa exacta.

## Abierto

- ¿Mensajes distintos por modo (Independent vs Managed usan límites
  distintos)?

## Estado

`idea`
