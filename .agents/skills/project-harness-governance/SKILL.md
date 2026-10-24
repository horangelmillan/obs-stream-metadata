---
name: project-harness-governance
description: Gobierno documental de obs-stream-metadata. Usar cuando una tarea requiera modificar documentación del proyecto: fuentes canónicas, resolución de discrepancias con evidencia, etiquetas [C]/[D]/[I]/[P] y límites de privacidad y operaciones.
---

# Project Harness Governance

Skill compartida de `obs-stream-metadata`. La cargan `harness-steward`
(siempre al modificar documentación) y los agentes de dominio
(`obs-plugin`, `backend-integrations`) solo cuando su tarea requiera
tocar docs. No sustituye a `AGENTS.md` §55; lo aplica.

## 1. Fuente canónica por tema (AGENTS.md §55)

| Tema | Fuente canónica | Los demás solo enlazan |
|---|---|---|
| Límites de producto y reglas duraderas | `AGENTS.md` | — |
| Estado de tareas y trabajo pendiente | `docs/BACKLOG.md` | STATE resume; resto enlaza |
| Resumen actual reconciliado | `docs/STATE.md` | No es backlog ni inventario |
| Arquitectura vigente | `docs/ARCHITECTURE-BACKEND.md` | ADRs = historia, no estado |
| Despliegue y estado operativo | `docs/DEPLOYMENT.md` | Contrato + drift conocido |
| Evidencia histórica | `docs/VALIDATION.md` (solo añadir) | No reescribir el pasado |
| Recuperación de contexto | `docs/PROJECT_CONTEXT.md` | Resumen con enlaces |
| Resumen para personas | `README.md` | Refleja capacidades, enlaza |
| Hallazgos | `docs/FINDINGS.md` (añadir F-###) | No reescribir filas |
| Principios/restricciones duraderas de privacidad | `AGENTS.md` (normativo) | PRIVACY describe; código/ARCH implementan |
| Tratamiento, retención, datos, comunicaciones al usuario | `docs/PRIVACY.md` (descriptivo) | No es norma; se corrige hacia código/ARCH |
| Implementación técnica de la privacidad | código vigente + `docs/ARCHITECTURE-BACKEND.md` | PRIVACY enlaza; no la redefine |

Si dos documentos discrepan, manda la fuente canónica y se registra la
discrepancia en el informe (y en FINDINGS si es doc-vs-código). En
privacidad, el principio de `AGENTS.md` prevalece; el detalle
descriptivo se corrige hacia el código/la arquitectura. Si no hay base
para una jerarquía coherente, documentar el hueco como `[P] provisional`
sin inventar decisiones de producto. No resolver contradicciones
concretas ni reescribir `PRIVACY.md` al definir autoridad.

## 2. Procedimiento de discrepancias

1. Localizar la afirmación en el checkout, el historial (`git log`,
   `git show`) y la documentación.
2. Comparar contra la fuente canónica del tema.
3. Resolver con evidencia: comando + salida, o cita [D] con fuente y
   fecha. Nunca reescribir historia: lo superado se etiqueta como
   histórico con su fecha y se conserva.
4. No cambiar estados del BACKLOG sin evidencia. No afirmar datos de
   producción como verificados sin haberlos comprobado en la sesión.
5. Tras editar, solo `git diff --check` (comprobación documental).

## 3. Etiquetas obligatorias

- **[C]** comprobado en esta sesión (comando + salida).
- **[D]** documentado (fuente + fecha, no re-ejecutado aquí).
- **[I]** inferido (marcar el salto explícitamente).
- **[P]** pendiente de confirmar.

Los datos fechados (revisiones, URLs, E2E, dictámenes GCP) son
evidencia histórica hasta volver a verificarlos; nunca se convierten
en reglas permanentes.

Rutas excluidas (`staging/`, `dev.env` y análogas; AGENTS.md §55.10):
registrar `git status --porcelain` inicial y final solo por lectura y
usar solo la formulación sostenible: (a) alcance del procedimiento
("no dirigido a esta ruta") = [C] de procedimiento; (b) igualdad del
`status` inicial/final = [C] de observación, nunca de contenido;
(c) identidad de contenido = solo con comprobación de contenido
autorizada; sin ella, el salto es [I] y sin base es [P]. Si la lectura
o huella está prohibida, reportar la limitación y quedarse en (a)/(b).

## 4. Documentación oficial con MCP/web

Consultar fuente oficial cuando una afirmación documental dependa de
sintaxis, configuración, cuotas o comportamiento de un proveedor,
SDK o API (OBS, Qt, CMake, OAuth de Twitch/YouTube/Kick/Meta, GCP):

- Preferir MCP `context7` (`resolve-library-id` → `query-docs`) para
  dependencias; `codebase-memory` para decisiones internas previas.
- `websearch` solo para detectar cambios; el snippet nunca es
  autoridad sin la fuente oficial detrás.
- Registrar cada consulta: `> Fuente: <url> — vX.Y — consultado
  YYYY-MM-DD — afirma: <cita breve>` (convención `docs/RESEARCH.md`).
- `github` MCP: no disponible sin autenticación en este entorno;
  no afirmar acceso remoto.

## 5. Resúmenes derivados sin fuentes paralelas

- STATE y PROJECT_CONTEXT resumen y enlazan; no duplican BACKLOG,
  ARCHITECTURE ni DEPLOYMENT.
- README refleja capacidades para personas; el detalle vive en docs.
- Al actualizar un resumen, verificar que no contradice su fuente
  canónica en la misma sesión.
- Los planes del operador/usuario se registran como [P] con origen y
  fecha; sin afirmar estados, aprobaciones, requisitos ni acciones
  no evidenciadas. La preparación de la app de Meta y las acciones
  del operador no son tareas de agentes.
- Verificación operativa (AGENTS.md §55.11): `/health`, `/ready` o
  `/version` no prueban el destino de un job de Cloud Scheduler. El
  destino operativo exige evidencia del propio job por consulta de solo
  lectura autorizada; sin ella, [D] o [P] según lo disponible.

## 6. Límites duros

- **Secretos/privacidad:** nunca leer, imprimir, registrar ni incluir
  en prompts `dev.env`, tokens, claves, URLs de conexión completas
  ni datos personales. Logs y mensajes: solo plataformas, códigos y
  longitudes.
- **Autorizaciones:** sin commit, push, PR, merge, despliegue,
  cambios IAM/secretos/scheduler/cuotas/URLs ni operaciones externas
  sin autorización explícita separada. `ask` en permisos = pedir al
  orquestador, nunca auto-aprobarse.
- **Un escritor a la vez** en el checkout; análisis de solo lectura
  en paralelo. Cada informe final lista archivos afectados,
  evidencia, verificaciones ejecutadas y bloqueos.
- **Neon es externo:** solo integración, no administración.
