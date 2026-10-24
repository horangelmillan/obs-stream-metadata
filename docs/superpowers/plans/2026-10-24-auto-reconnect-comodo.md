# T-075 Auto-reconnect cómodo Managed — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Que el usuario no tenga que reconectar a mano salvo revocación real del proveedor.

**Architecture:** Solo plugin Qt; backend sin cambios. Desglosar error genérico, revalidación silenciosa al arrancar, un reintento acotado antes de pedir Connect.

**Tech Stack:** C++ Qt6, QNAM async, backend_auth Client existente.

**Spec:** docs/STATE.md + conversación usuario 2026-10-24 (comodidad, no pedir reconexión intermitente).

## Global Constraints

- UI nunca se bloquea; todo async QNAM.
- Cero secretos en logs/mensajes/repo.
- Sin polling continuo; YouTube cuota 10k/día.
- Una falla no cancela otras plataformas.
- 429 sin reintentos agresivos; 5xx backoff acotado existente.
- Español UI coherente con resto.

---

### Task 1: Desglosar mensaje Backend authentication failed

**Files:**
- Modify: `src/metadata_dock.cpp:1146-1159` (`managedAuthError`)

**Interfaces:**
- Consumes: `backend_auth::Result r`
- Produces: mensajes específicos por rama.

- [ ] **Step 1: Leer función actual**

Ver `managedAuthError`: RateLimited, NetworkError, StorageError, default.

- [ ] **Step 2: Escribir mapeo nuevo**

```cpp
// en managedAuthError:
case Unauthorized: return tr("Session expired. Retrying silently… (press Connect only if it persists)");
case ServerError: return tr("Managed backend error (temporary). Retrying once…");
case InvalidResponse: return tr("Unexpected backend reply. Retrying once…");
default: anterior
```

Mantener RateLimited/Network/Storage iguales. Sin secretos.

- [ ] **Step 3: Verificar compilación de este archivo**

Run: `cmake --build build_x64 --config RelWithDebInfo` (o el preset vigente)
Expected: exit 0 en este fichero.

### Task 2: Revalidación silenciosa al arrancar (sin navegador)

**Files:**
- Modify: `src/metadata_dock.h` (declarar `silentRevalidateManaged()`)
- Modify: `src/metadata_dock.cpp` (`initManaged`, tras `loadStore`)

**Interfaces:**
- Consumes: `managedAccount(p).connected` snapshots T-051
- Produces: estados `Connected as` / `Needs reconnection` sin abrir navegador.

- [ ] **Step 1: Failing check manual**

Arrancar OBS con snapshot Managed conectado y backend caído → hoy dice Not connected sin intentar nada.

- [ ] **Step 2: Implementar**

```cpp
void MetadataDock::silentRevalidateManaged() {
  if (!isManaged()) return;
  for (p in {Twitch,YouTube,Kick,Facebook}) {
    if (!managedAccount(p).connected) continue;
    setStatus(p, tr("Checking session…"));
  }
  managedAuth_->ensureSession([this](Result r){
    if (r != Ok) { /* marcar Checking failed, no error rojo; próximo Connect lo arregla */ return; }
    for cada p con snapshot: apiGet /connect/<slug>/status → si connected: finishManagedConnected silencioso; si no: marcar Needs reconnection sin borrar snapshot hasta Apply/Connect.
  });
}
```

Llamar al final del constructor tras `initManaged()` solo si `isManaged()`. Sin `openBrowser`, sin polling, una sola pasada. Log solo `managed silent revalidate result=%d`.

- [ ] **Step 3: Build**

Run: build parcial.
Expected: PASS.

### Task 3: Un reintento auto antes de pedir Connect en Apply

**Files:**
- Modify: `src/metadata_dock.cpp` (`startManagedYouTubeApply`, `startManagedTwitchApply`, `startManagedKickApply`, `startManagedFacebookApply`)

**Interfaces:**
- Consumes: `ApiReply{result,http,body}`
- Produces: `finishPlatform` o un reintento único.

- [ ] **Step 1: Test manual**

Apply con sesión plugin caducada → hoy marca Needs reconnection directo.

- [ ] **Step 2: Implementar regla**

Si `rep.result==Unauthorized && rep.http==0` (falló capa plugin-backend, no proveedor):
  `managedAuth_->ensureSession` una vez → si Ok, reintentar el mismo `apiPost` una vez; si falla de nuevo, marcar Needs reconnection.
Si `http==401/403` del proveedor: marcar Needs reconnection directo (revocado), sin reintentar.

Contador por plataforma (reutilizar `retried_` existente, no nuevo loop).

- [ ] **Step 3: Build**

### Task 4: Documentar causa Google Testing 7 días

**Files:**
- Modify: `docs/TROUBLESHOOTING.md` (añadir T-075)
- Modify: `docs/STATE.md` (línea estado)

- [ ] **Step 1: Añadir nota**

Si Google OAuth consent en Testing, refresh caduca 7 días → `invalid_grant` → reconexión obligatoria. Pasar a In production. Kick 30d sliding, Twitch largo. Nuestra sesión 30min se renueva sola.

- [ ] **Step 2: Verificar docs**

Run: `grep -r secret` scan vigente.
Expected: PASS.

### Task 5: Validación

- [ ] **Step 1: Backend tests**

Run: `python -m unittest discover -s backend/tests`
Expected: 0 failures.

- [ ] **Step 2: Selfcheck + build**

Run: build + `metadata-selfcheck.exe`
Expected: SELFCHECK OK.

- [ ] **Step 3: Commit/PR**

`git add` solo archivos tocados. Commit `feat(T-075): auto-reconnect cómodo Managed`. Push rama, PR con template, CI verde, merge solo tras aprobación usuario.
