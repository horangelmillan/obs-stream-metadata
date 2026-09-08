# ADR-007 — `buildspec.json` propio para OBS 32.2.2 (P1)

- **Fecha:** 2026-09-08 · **Tarea:** T-013 · **Estado:** decidido y validado (build + carga reales).
- **Problema:** el template oficial fija obs-studio 31.1.1 + obs-deps 2025-07-11; nuestro target es OBS 32.2.2 (ADR-005). Hay que subir versiones sin inventar hashes (el bootstrap verifica `EXPECTED_HASH SHA256`).
- **Investigación (evidencia):**
  - Tag `obsproject/obs-studio@32.2.2` (`CMakePresets.json`, vendor `obsproject.com/obs-studio/dependencies`): `prebuilt` y `qt6` versión **2026-07-15** con hashes oficiales windows-x64 (`6f90e959…`, `7c7f9857…`), macos-universal (`4ecb4c59…`, `d4b80586…`) y debugSymbols windows-x64 (`471d0b21…`).
  - Archivo obs-studio: el template descarga `{baseUrl}/32.2.2.zip` (windows) y `32.2.2.tar.gz` (macos); SHA256 calculados por descarga directa el 2026-09-08: zip `f15f001f…831aa` (18.793.846 bytes), tar.gz `35d3cd09…72626c4`.
  - Requisitos del tag 32.2.2: `cmake_minimum_required(VERSION 3.28...3.30)`, SDK ≥ 10.0.20348, C17/C++17 — compatibles con ADR-005 (VS2022 + SDK 22621 + CMake 3.31.6). El VS2026/SDK26100 del CI oficial solo aplica a compilar OBS completo, no a plugins vía template (cierra F-008).
  - Qt del deps 2026-07-15 = 6.11.1.0, idéntico al Qt embebido de OBS 32.2.2 instalado (cierra R2 de ADR-005).
- **Elección:** vendorizar el template con `buildspec.json` propio (`obs-studio 32.2.2`, `prebuilt/qt6 2026-07-15`, hashes verificados) + nombre `obs-stream-metadata` v0.1.0; preset `windows-x64` intacto (VS17 2022, SDK 22621); configure con `-DENABLE_FRONTEND_API=ON -DENABLE_QT=ON -DCMAKE_SYSTEM_VERSION=10.0.22621.0`.
- **Consecuencias:**
  - P1 compila y carga en OBS 32.2.2 (T-013). P2 construye el dock sobre esta base sin tocar el bootstrap.
  - Si OBS publica 32.3.x, actualizar `buildspec.json` con el mismo procedimiento (hashes por descarga + versión de deps del tag), no a ciegas.
