# T-076 Facebook gris + YouTube default listo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Sin alarmas por plataformas no conectadas y broadcast correcto por defecto.

**Architecture:** Plugin filtra Apply a conectadas; backend ordena broadcasts (activo, próximos por fecha, terminados); selects index 0 + tooltip.

**Tech Stack:** C++ Qt6, Python stdlib backend.

**Spec:** Conversación usuario 2026-10-24 + docs YouTube liveBroadcasts.list 2026-10-15 (mine=true sin orden garantizado).

## Global Constraints

- Sin polling; YouTube 10k u/día.
- Sin secretos en logs/mensajes.
- Una falla no cancela otras.
- UI no se bloquea.

---

### Task 1: Facebook gris (ya implementado en este diff)

**Files:** `src/metadata_dock.cpp` (onApply filtra por `platformUsable`).

- [x] Solo entran conectadas; resto se salta en silencio.
- [x] Si ninguna conectada: mensaje amable, sin rojos.

### Task 2: Backend orden listo (ya implementado)

**Files:** `backend/adapters/youtube.py`, `backend/tests/test_metadata_youtube.py`.

- [x] `scheduledStart` en recursos; orden activo→próximos por fecha→terminados.
- [x] Test rojo→verde con 3 broadcasts desordenados.

### Task 3: Frontend default + etiqueta (ya implementado)

**Files:** `src/metadata_dock.cpp`.

- [x] Managed: etiqueta `título [estado] (id)` + `setCurrentIndex(0)`.
- [x] Independent: ordenar igual + etiqueta + `setCurrentIndex(0)`.
- [x] Tooltip en label+combo: para qué es el selector.
- [x] `#include <algorithm>`.

### Task 4: Validación

- [x] `python -m unittest discover -s backend/tests` → 250 OK (10 skipped).
- [ ] Build comercial + E2E operador (tú): instalar, conectar, verificar que el combo elige el activo/próximo y Facebook no da error sin conectar.
- [ ] Commit/PR → CI verde → merge tras tu E2E.
