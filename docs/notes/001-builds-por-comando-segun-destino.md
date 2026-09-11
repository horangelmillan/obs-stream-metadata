# 001 — Builds por comando según destino

- Estado: idea
- Contexto: al regenerar el instalador quedó claro que hoy solo existe
  "el build de desarrollo" y hay que recordar flags a mano. Distintos
  destinos necesitan distinta configuración y mezclarlos es fuente de
  errores (p. ej. un ID de Twitch de pruebas en un instalador comercial).

## Propuesta

Un único punto de entrada (p. ej. `tools/build.ps1 -Flavor <nombre>`)
que fije preset, flags y validaciones por sabor:

| Sabor | Uso | Backend | Credenciales |
|---|---|---|---|
| `commercial` | usuario final | producción | ninguna empaquetada (BYO) |
| `testing` | testers/desarrolladores | ambiente test | de test, nunca prod |
| `local` | desarrolladores | local (`localhost`) | locales vía `dev.env` |
| `prod-integrated` | casos críticos puntuales | **productivo** | solo con confirmación explícita |

Reglas sugeridas:

- Cada sabor valida su entorno antes de compilar (p. ej. `commercial`
  falla si detecta `OBS_TWITCH_CLIENT_ID` seteado o `dev.env` con
  valores productivos).
- `prod-integrated` debe pedir confirmación interactiva y dejar rastro
  en el log del build (qué, quién, cuándo, por qué).
- El nombre del instalador incluye el sabor
  (`...-commercial-...exe`) para que sea imposible confundirlos.
- Ningún sabor escribe secretos en el repo ni en el instalador.

## Relación

- Toca: `CMakeLists.txt` (flags), `docs/INSTALLER.md`, CI futuro.
- No toca: lógica del plugin/backend.
