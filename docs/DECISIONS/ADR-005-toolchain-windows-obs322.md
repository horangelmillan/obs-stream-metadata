# ADR-005 — Toolchain Windows fijado para el plugin OBS (target 32.2.2)

- **Fecha:** 2026-09-08 · **Tarea:** T-011 (decisión) / T-012 (instalación) · **Estado:** decidido e instalado (verificado 2026-09-08, F-008).
- **Problema:** fijar un entorno reproducible para compilar un plugin nativo OBS en Windows sin incompatibilidades binarias (especialmente Qt).

## Decisión

| Componente | Versión fijada | Fuente / evidencia |
|---|---|---|
| OBS objetivo | **32.2.2** (x64, instalado localmente) | `obs64.exe` local `32.2.2`; release oficial `github.com/obsproject/obs-studio/releases/tag/32.2.2` |
| OBS mínimo soportado | **≥ 30.0** (API de docks existe desde 30.0) | `AGENTS.md` §5, §34; ADR-003 |
| Qt | **Qt6 misma serie mayor que el OBS objetivo** — la que descarga el bootstrap del template vía `obs-deps` (no instalar un Qt arbitrario) | Qt6Core.dll local `6.11.1.0` (`C:\Program Files\obs-studio\bin\64bit`); discusión oficial `obsproject/obs-studio#6481` (plugins con Qt deben compilar contra Qt6 para OBS 28+) |
| Visual Studio | **17 2022** (Community vale) + workload "Desktop development with C++" + **C++ ATL** (x86/x64) | Wiki oficial `obs-plugintemplate` → Build System Requirements (ed. 2024-11-01); `obsproject.com/wiki/build-instructions-for-windows` |
| Windows SDK | **10.0.22621** (el instalador de VS lo provee; mínimo documentado por OBS 10.0.20348.0) | Wiki template (10.0.22621); wiki OBS (mínimo 10.0.20348.0) — discrepancia menor registrada, se adopta la del template por ser la guía de plugin-dev |
| CMake | **≥ 3.28** (`cmake_minimum_required(VERSION 3.28...3.30)` del template); recomendado **3.30.5** en Windows | `CMakeLists.txt` oficial del template (consultado 2026-09-08); wiki template |
| Base del proyecto | **Template oficial `obs-plugintemplate`** (rama `master`), `ENABLE_FRONTEND_API=ON` + `ENABLE_QT=ON` | `CMakeLists.txt` oficial (Qt6 Widgets/Core, AUTOMOC/AUTOUIC/AUTORCC); ADR-003 |
| Dependencias OBS/Qt | **Descarga automática** por el bootstrap CMake del template según `buildspec.json` (`obs-studio` sources + `obs-deps` prebuilt incl. Qt6) — nada manual | `buildspec.json` oficial (actualmente fija obs-studio **31.1.1** + deps 2025-07-11, ver riesgo R1) |

## Por qué OBS 32.2.2 y no "≥ 30.0 genérico"

- La API de docks (`obs_frontend_add_dock_by_id`) existe desde 30.0, así que 30.0 es el **mínimo** declarable.
- Pero el PoC debe compilarse y probarse contra el OBS **instalado y actual** (32.2.2): elimina riesgo de skew de versiones durante las pruebas reales (T-020/T-030) y coincide con `AGENTS.md` §34.
- Fijar target moderno concreto no impide declarar compatibilidad 30.0+ más adelante, cuando haya CI que lo verifique.

## Estado local verificado (2026-09-08, esta máquina — T-012)

- OBS 32.2.2 x64 instalado (`C:\Program Files\obs-studio`) con Qt 6.11.1 embebido (`Qt6Core.dll` 6.11.1.0 + Gui/Network/Svg/Widgets/Xml).
- **Instalado en T-012** (vía `winget`, procedimiento en `TROUBLESHOOTING.md`):
  - Visual Studio 2022 Community **17.14.37614.0** (`C:\Program Files\Microsoft Visual Studio\2022\Community`), workload C++ Desktop + ATL + SDK 22621 + CMake tools.
  - MSVC **19.44.35228** (toolset 14.44.35207, `Hostx64\x64\cl.exe`); MSBuild 17.14.51.
  - **ATL:** `VC\Tools\MSVC\14.44.35207\atlmfc\include\atlbase.h` presente.
  - Windows SDK: **`10.0.22621.0`** presente (además 10.0.26100.0, traído por VS; CMake auto-selecciona el más nuevo si no se fija `CMAKE_SYSTEM_VERSION`).
  - CMake **3.31.6-msvc6** (bundled VS; sin instalación separada, según wiki template: el CMake integrado hace opcional la instalación aparte). Acepta el rango del template `cmake_minimum_required(VERSION 3.28...3.30)` — configure de prueba ok con generador "Visual Studio 17 2022".
  - Git for Windows **2.49.0.windows.1** (preexistente).
- **Qt NO instalado manualmente:** sin `C:\Qt`, sin `qmake` en PATH (verificado). Lo proveerá el bootstrap del template (obs-deps prebuilt Qt6) en P1.

## Qué instalar (ejecutado en T-012, 2026-09-08 — NO repetir sin motivo)

1. Visual Studio 2022 Community (workload C++ Desktop + ATL + Windows SDK 10.0.22621 o superior compatible).
2. CMake ≥ 3.28 (3.30.5 recomendado; el propio VS trae soporte CMake integrado como alternativa).
3. Git for Windows (ya disponible: este repo lo usa).
4. Qt **NO se instala manualmente**: lo provee el bootstrap del template (obs-deps prebuilt Qt6). Instalar un Qt6 cualquiera del sitio de Qt es la vía documentada solo para compilar OBS completo, no para plugins vía template — y es la principal fuente de incompatibilidad binaria a evitar.

## Riesgos

- **R1 — El template fija obs-studio 31.1.1, no 32.2.2.** En Phase 1 habrá que subir `buildspec.json` a 32.2.2 y compilar; si el template aún no lo soporta, registrar FINDING y evaluar (a) esperar update del template o (b) target 31.1.1 temporal con OBS 31 instalado en paralelo. No resolver por intuición.
- **R2 — Qt 6.11.1 local vs Qt del deps que descargue el template.** Si difieren de serie menor, Qt6 mantiene compatibilidad binaria dentro del major, pero la regla es probar el plugin cargado en OBS 32.2.2 (T-020) y no darlo por válido por inspección.
- **R3 — Wiki del template editada 2024-11-01** (pre-OBS 32): VS/SDK/CMake siguen vigentes según README actual del template, pero re-verificar contra el template en Phase 1 antes de instalar.
- **R4 — Solo Windows x64 en MVP.** Sin soporte x86/macOS/Linux hasta que el MVP sea estable (`AGENTS.md` §6: no introducir dependencias Windows-only innecesarias en el código, aunque el target inicial sea Windows).

## Consecuencias

- Phase 1 empieza instalando VS2022 + CMake y clonando el template como referencia (no copiarlo aún al repo sin plan de Phase 1).
- `docs/STATE.md` y `F-002` se actualizan con esta decisión; la instalación real queda como primera tarea de Phase 1.
