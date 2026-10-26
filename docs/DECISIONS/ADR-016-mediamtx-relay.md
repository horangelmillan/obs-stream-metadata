# ADR-016 — Integración del relay local MediaMTX al producto (Fase 1: decisión documental)

- **Fecha:** 2026-10-26 · **Tareas:** T-078 (Fase 1 del port relay→producto) · **Estado:** decidido (documental); implementación y E2E pendientes.
- **Autorización:** dueño 2026-10-26 [D] (autorización en chat para continuar con la integración del repetidor MediaMTX al plugin/instalador, levantando `AGENTS.md` §§23/51 para este flujo).
- **Relación:** no modifica el resto de `AGENTS.md` §§23/51 (excepción acotada solo a este relay, ver §3). Flujo técnico en `docs/MEDIAMTX-RELAY-FLOW.md`. Evidencia E2E lab en `docs/VALIDATION.md` T-078 + F-092–F-099. T-077 intacta en investigación.

## 1. Contexto

T-078 validó en lab 2026-10-26 [D: `docs/VALIDATION.md` T-078; F-092–F-099] que un relay local MediaMTX resuelve el problema de la URL de ingesta cambiante de cada LiveVideo propio (F-092): el operador fija un único destino RTMP (`rtmp://127.0.0.1:1935/live`) y el relay reenvía a la ingesta RTMPS del LiveVideo vigente. E2E lab: forward RTMPS a LiveVideo de prueba PASS + cambio de destino en caliente sin reconfigurar Aitum PASS (2 emisiones LIVE+DIRECTO sin tocar Aitum, PATCH sin reinicio) [D]. Formato ganador: base+`#`+clave, jamás URL íntegra (F-096). Pendientes conocidos: métricas latencia/CPU/recovery (F-099), audiencia en producción (F-093), cooldown ~3 min entre re-handshakes (F-097), auto-VOD al retirar señal (F-098), aviso de licencia MIT si se empaqueta, ciclo de vida del proceso hijo, puertos 1935/9997, firewall Windows, bus-factor/salud MediaMTX [D/P según fila].

## 2. Decisión

Integrar el relay local MediaMTX al producto con instalación integrada y gestión desde el plugin:

- El instalador incluye el binario MediaMTX (MIT; aviso de licencia incluido, ver §5) o lo obtiene de forma reproducible y verificada; el plugin lo lanza/gobierna como proceso hijo en el PC del usuario.
- El plugin fija el forward vía Control API (`:9997`) hacia la ingesta RTMPS del LiveVideo propio vigente en cada emisión (formato base+`#`+clave, F-096).
- Alcance exacto: **el plugin solo fija el forward**; **NO automatiza Aitum ni OBS**: el operador mantiene su destino fijo `rtmp://127.0.0.1:1935/live` configurado una vez a mano. El plugin no toca configuración RTMP, stream keys, outputs, servicios, escenas ni fuentes de OBS/Aitum.

## 3. Lo que cambia de `AGENTS.md` §§23/51 (excepción acotada)

- §23 ("El plugin solamente modifica metadata… No modificar: configuración RTMP… Esto permite que el plugin sea independiente del sistema de multistream") y §51 (lista de fuera de alcance: stream keys, multistream, etc.) reciben una **excepción acotada solo a este relay**: el plugin gobierna un proceso hijo MediaMTX local y fija su forward por Control API, manteniendo el destino del operador fijo.
- Resto de límites intactos: sin multistream general (un solo destino fijo reenviado, no distribución multi-plataforma), sin keys en repo, sin tocar outputs/servicios/escenas/fuentes de OBS, sin descubrir ni automatizar Aitum (§24 intacto: sin archivos internos, APIs privadas ni scraping).
- Historia conservada: las notas de vigencia previas de §§23/31/51 no se reescriben; la excepción se añade como nota fechada 2026-10-26 con enlace a este ADR.

## 4. Requisitos pendientes de T-078 (deben resolverse antes de darla por hecha)

1. Métricas de lab: latencia añadida, CPU en el PC del usuario y recovery ante caída del relay o del destino (F-099) [P].
2. Audiencia en producción (F-093: privacy ilegible + badge "Solo yo" en lab) [P].
3. Cooldown ~3 min entre re-handshakes RTMPS seguidos (F-097): el producto debe respetarlo, sin reintentos agresivos [P].
4. Auto-VOD al retirar señal (F-098): contar con ello en el flujo de producto (cierre deja VOD salvo borrado explícito) [P].
5. Aviso de licencia MIT en el instalador si se empaqueta MediaMTX (atribución conforme a su licencia) [P].
6. Ciclo de vida del proceso hijo (arranque/parada/supervisión, sin huérfanos ni doble instancia) [P].
7. Puertos 1935/9997: ocupación previa, convivencia con OBS, mensajes claros al operador [P].
8. Firewall de Windows: reglas/permisos documentados en el instalador [P].
9. Bus-factor / salud del proyecto MediaMTX como dependencia (mantenimiento, releases, riesgo de abandono; plan si se abandona) [P].

## 5. Consecuencias

- `docs/MEDIAMTX-RELAY-FLOW.md` pasa de propuesta `[P]` a contrato vigente en lo aplicable (diagrama + forward en caliente + formato F-096); las preguntas abiertas que sigan sin evidencia quedan como `[P]` explícitos.
- `.gitignore`: los binarios del relay no se versionan jamás (ni en `lab/` ni empaquetados); solo scripts/configs reproducibles + checksum.
- `PRIVACY.md`: el relay local no añade tratamiento de datos (solo reenvía bytes de vídeo en el PC del usuario); si la implementación futura registrara algo, se inventaría entonces [I: sin base en código aún, marcar como pendiente de revisión cuando exista implementación].
- Implementación futura (fuera de esta Fase 1 documental): proceso hijo, Control API `:9997`, instalador con aviso MIT, firewall, E2E de aceptación.

## 6. Criterios de aceptación para dar T-078 por hecha (E2E + evidencia)

1. Instalación integrada: instalar desde el instalador deja MediaMTX operativo + aviso MIT visible [P: E2E operador pendiente].
2. Destino fijo: Aitum/OBS apunta una sola vez a `rtmp://127.0.0.1:1935/live` y no se reconfigura entre emisiones [P].
3. Forward en caliente: el plugin fija el forward al LiveVideo vigente vía `:9997` sin reiniciar el relay ni cortar la fuente (formato F-096) [P].
4. Emisión completa: preview → GoLive → LIVE visible → End → VOD esperado (F-098 tenido en cuenta) [P].
5. Cooldown respetado (F-097): reintentos sin agresión al límite ~3 min [P].
6. Métricas registradas: latencia/CPU/recovery (F-099 cerrada con números) [P].
7. Audiencia producción confirmada (F-093 cerrada fuera de lab) [P].
8. Puertos/firewall/ciclo de vida: comportamiento documentado + mensajes al operador [P].
9. Evidencia en `docs/VALIDATION.md` (comando + salida o E2E del operador registrado) + `git diff --check` + secret-scan limpios antes de cualquier commit.
