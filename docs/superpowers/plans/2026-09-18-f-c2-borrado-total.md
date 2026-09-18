# F-C2 Borrado total por instalación — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Endpoint `POST /privacy/erase` + botón "Borrar mis datos" que borran todos los datos Managed de una instalación (connections + tokens no compartidos + sessions + transactions + fila installation), con revoke remoto best-effort y `PRIVACY.md` con contacto y retención.

**Architecture:** Nuevo `backend/privacy.py` orquesta `ConnectService.erase_local()` (extraído de `disconnect`, con guarded token-delete) por provider; purga sessions/transactions/installation; revoca al final best-effort. 4 métodos aditivos en ports, sin migración. C++ mínimo: slot + snapshots + nota NSIS.

**Tech Stack:** Python stdlib backend, Qt6 C++ (solo slot/snapshots), NSIS (1 MessageBox), docs.

**Spec:** F-C2 (`docs/FASES-COMERCIAL.md`), T-064 (`docs/BACKLOG.md`), decisión §2.3 abajo (vale como spec de alcance).

## Global Constraints

- Sin tocar OAuth, flujos Connect/Apply, ni DCF. Reutilizar `ensure_fresh_token`/auth/stores/límites existentes.
- Respuestas y logs sin secretos: solo conteos, providers, códigos, requestId. `installation_id` (UUID no secreto) se omite de logs por minimización.
- F-030: borrado local primero, revoke remoto best-effort después (un fallo de red jamás resucita estado).
- Uninstaller nunca toca `%APPDATA%` (restricción vigente): solo nota informativa.
- Commits en español, PR con template, CI verde, squash. SIN COMMIT sin autorización del usuario.
- `python -m unittest discover -s backend/tests` en verde + selfcheck OK antes de cerrar.

---

## Decisión 2.3 (spec de alcance, con alternativas descartadas)

**"Borrado total por instalación" =** todas las filas `connections` de la instalación (todos los providers) + filas `tokens` asociadas **solo si ninguna otra instalación las referencia** + todas las `sessions` de la instalación + `transactions` pendientes de la instalación + fila `installations` (DELETE, no solo flag `revoked`).

- **Tokens compartidos (tabla `tokens` por `(provider, provider_user_id)` sin `installation_id`):** NO impide el borrado limpio sin cambio arquitectónico → NO procede detenerse. Solución sin migración: `ConnectionStore.list_referencing(provider, user_id) -> list[installation_id]` (scan en Python, mismo patrón que `PgOAuthTransactionStore.find_by_state`) y borrado condicional de la fila. El revoke remoto se ejecuta SIEMPRE (también si compartido: el grant OAuth es por (app, usuario), indivisible; la instalación superviviente recibe `SESSION_EXPIRED` → reconectar; documentado en PRIVACY).
- **Orden (justificado por F-030):** copiar tokens a memoria → DELETEs locales (connections, tokens no compartidos, sessions, transactions, installation) → revoke remoto best-effort (fallos tragados). La garantía de borrado nunca depende de la red.
- **UX: botón dedicado "Borrar mis datos"**, NO extender Disconnect. Disconnect es por proveedor y conserva instalación/sesiones; el borrado es por instalación y destruye la identidad. Semánticas distintas; mezclarlas confunde y arriesga borrados accidentales.
- **Snapshots locales:** el botón limpia snapshots Managed (youtube/kick/twitch-managed) + registro `backendInstall` local; registros Independent intactos (tienen su Disconnect propio + DPAPI).
- **Uninstaller:** solo nota (`un.onInit` MessageBox informativo, sin abortar). Borrar `%APPDATA%` violaría la restricción vigente y arriesga datos de OBS.
- **Descartadas:** (a) columna `installation_id` en `tokens` — migración + cambia PK y semántica de refresh compartido; innecesario con guarded-delete. (b) DELETE en cascada SQL — el esquema guarda JSON en TEXT sin FKs; el scan en Python es el patrón existente. (c) Borrar `%APPDATA%` desde NSIS — prohibido por restricción vigente. (d) Extender Disconnect — semántica distinta (ver arriba).

**Evidencia externa (consultada 2026-09-18):** RGPD art. 17.1.a/b (supresión sin dilación indebida cuando los datos ya no son necesarios o se retira el consentimiento; respuesta en 1 mes, art. 12.3; ninguna excepción del 17.3 aplica a tokens OAuth) — eur-lex.europa.eu, crowd.legal, rgpd.com. Google: `POST https://oauth2.googleapis.com/revoke` (revocar access revoca también su refresh) + policy exige informar retención/borrado en la privacy policy (developers.google.com, context7 `/websites/developers_google_identity_protocols_oauth2`). Twitch: `POST https://id.twitch.tv/oauth2/revoke` (client_id+token; 200 OK, 400 token inválido = objetivo cumplido, 404 bad client), verificación vía `GET /validate` (dev.twitch.tv/docs/authentication/revoke-tokens, consultado 2026-09-18). Kick: `POST https://id.kick.com/oauth/revoke?token=&token_hint_type=`, verificación `POST /oauth/token/introspect` con Bearer (KickDevDocs generating-tokens-oauth2-flow.md). Logs: request IDs + IDs opacos, redacción al escribir, retención documentada con base legal (patrón ya implementado en `RedactingFilter`).

---

## Contrato del endpoint (diseño)

```text
POST /privacy/erase
Auth:    Bearer <session_token> vigente (check_access existente; prueba propiedad de la instalación)
Rate:    auth_install existente (f"{installation}-erase"), global existente
Body:    {} (objeto JSON, sin campos)
Resp 200:
  {"erased": {"connections": 2, "tokens": 2, "sessions": 1,
              "transactions": 0, "installation": 1},
   "revoked": {"youtube": true, "kick": true}}
Errores: 401 sesión inválida/expirada (existente); 429 rate-limit (existente);
         500 genérico sin detalle (existente). Sin códigos nuevos.
Idempotencia: instalación desconocida o ya borrada → 200 con todo a cero.
Post-condición: el bearer usado queda revocado (era una session de la
instalación); /status → disconnected; reconectar exige OAuth nuevo.
```

---

### Task 1: Métodos de purga en ports + stores (InMemory + SQLite + PG)

**Files:**
- Modify: `backend/ports.py` (4 firmas)
- Modify: `backend/stores.py` (`InMemoryConnectionStore.list_referencing`, `InMemorySessionStore.delete_for_installation`, `InMemoryOAuthTransactionStore.delete_for_installation`, `InMemoryInstallationStore.delete`)
- Modify: `backend/prodstores.py` (mismos 4 en `Sqlite*`)
- Modify: `backend/pgstores.py` (mismos 4 en `Pg*`)
- Test: `backend/tests/test_privacy.py` (nuevo; store-level + servicio + HTTP en tasks 3-4)

**Interfaces:**
- Consumes: nada nuevo (payloads `sessions` = `{"installation_id": ...}`, `transactions` = `{"installation_id": ...}`, `connections` entry = `{"account": {"provider_user_id": ...}}`).
- Produces: `list_referencing(provider: str, provider_user_id: str) -> list[str]`; `delete_for_installation(installation_id: str) -> int` (sessions y transactions); `delete(installation_id: str) -> None` (installations). Nombres exactos, usados por tasks 2-4.

- [ ] **Step 1: test rojo — `list_referencing` y `delete_for_installation` en InMemory**

```python
def test_list_referencing_and_session_purge(self):
    from backend.stores import (InMemoryConnectionStore, InMemorySessionStore)
    conns = InMemoryConnectionStore()
    conns.save("inst-a", "youtube", {"account": {"provider_user_id": "UC1"}})
    conns.save("inst-b", "youtube", {"account": {"provider_user_id": "UC1"}})
    self.assertEqual(sorted(conns.list_referencing("youtube", "UC1")),
                     ["inst-a", "inst-b"])
    sess = InMemorySessionStore()
    sess.save_session("s1", {"installation_id": "inst-a"})
    sess.save_session("s2", {"installation_id": "inst-b"})
    self.assertEqual(sess.delete_for_installation("inst-a"), 1)
    self.assertIsNotNone(sess.load_session("s2"))
```

Run: `python -m unittest backend.tests.test_privacy -v`. Expected: FAIL/ERROR (`AttributeError`, método inexistente = feature missing, correcto).

- [ ] **Step 2: verde mínimo en `backend/stores.py`**

```python
def list_referencing(self, provider: str, provider_user_id: str) -> list:
    return sorted(iid for (iid, prov), e in self._data.items()
                  if prov == provider and e.get("account", {}).get("provider_user_id") == provider_user_id)

def delete_for_installation(self, installation_id: str) -> int:
    doomed = [sid for sid, p in self._data.items()
              if p.get("installation_id") == installation_id]
    for sid in doomed:
        del self._data[sid]
    return len(doomed)
```

(Misma forma para transactions — scan de `entry.get("installation_id")` — e `installations.delete` = `self._data.pop(installation_id, None)`.)

- [ ] **Step 3: test rojo — mismos métodos en SQLite y PG**

```python
def test_sqlite_purge_methods(self):
    from backend.prodstores import SqliteConnectionStore, SqliteSessionStore
    import sqlite3
    conn = sqlite3.connect(":memory:", check_same_thread=False)
    conn.execute("CREATE TABLE connections (installation_id TEXT, provider TEXT, entry_json TEXT)")
    store = SqliteConnectionStore.__new__(SqliteConnectionStore)
    import threading as _t
    store._conn, store._lock = conn, _t.Lock()
    store.save("inst-a", "youtube", {"account": {"provider_user_id": "UC1"}})
    self.assertEqual(store.list_referencing("youtube", "UC1"), ["inst-a"])
```

(Expected: FAIL hasta implementar; PG cubierto por `test_pg.py` si hay servidor, si no skip por diseño — ver Step 4.)

- [ ] **Step 4: verde en `prodstores.py` y `pgstores.py`** — scan en Python sobre `SELECT entry_json/payload_json` + `json.loads` (idéntico patrón a `find_by_state` existente); `DELETE FROM sessions WHERE id=?` por id recolectado; `DELETE FROM installations WHERE id=?`. PG igual con `%s`. Verificar `python -m unittest discover -s backend/tests` en verde.

- [ ] **Step 5: fakes de `test_environment.py`** — añadir `delete_for_installation`/`delete`/`list_referencing` no-op a `_ProdSessions`/`_ProdInstallations` SOLO si algún gate los llama (no los llama; verificar y no tocar si no hace falta — ponytail: no tocar).

### Task 2: `ConnectService.erase_local()` + disconnect con guarded-delete

**Files:**
- Modify: `backend/oauth.py:179-203` (extraer `erase_local`, reescribir `disconnect`)
- Test: `backend/tests/test_privacy.py` (`EraseLocalTest`)

**Interfaces:**
- Consumes: `ConnectionStore.list_referencing` (Task 1).
- Produces: `erase_local(installation_id) -> tuple[dict|None, TokenPair|None]` = (entry, tokens copiados en memoria). Usado por `disconnect` y `privacy.erase`.

- [ ] **Step 1: test rojo — disconnect NO borra token compartido con otra instalación**

```python
def test_disconnect_keeps_shared_token(self):
    svc_a, _ = make_service(fake_google_ok)   # helper existente de test_youtube
    # misma cuenta (UC123) conectada en inst-a e inst-b compartiendo stores
    ...
    svc_a.disconnect("inst-a")
    self.assertEqual(svc_b.status("inst-b")["status"], "connected")
    self.assertIsNotNone(tokens.load(account_uc123))  # fila preservada
```

(Expected: FAIL — hoy `disconnect` borra la fila incondicionalmente.)

- [ ] **Step 2: verde mínimo en `backend/oauth.py`**

```python
def erase_local(self, installation_id: str):
    """Borrado local de UNA conexión; devuelve (entry, tokens) para revoke posterior (F-030)."""
    entry = self._connections.load(installation_id, self._provider.provider.value)
    if entry is None:
        return None, None
    account_data = entry["account"]
    from backend.kernel import Account as _Account
    account = _Account(provider=self._provider.provider,
                       provider_user_id=account_data["provider_user_id"],
                       display_name=account_data["display_name"],
                       scopes=tuple(account_data["scopes"]))
    tokens = self._tokens.load(account)
    self._connections.delete(installation_id, self._provider.provider.value)
    if tokens is not None:
        refs = self._connections.list_referencing(
            self._provider.provider.value, account.provider_user_id)
        if not refs:
            self._tokens.delete(account)
    return entry, tokens

def disconnect(self, installation_id: str, revoke_remote: bool = True) -> None:
    """Borrado local siempre; revoke remoto best-effort (F-030)."""
    _, tokens = self.erase_local(installation_id)
    if revoke_remote and tokens is not None:
        for token in (tokens.access_token, tokens.refresh_token):
            if token:
                try:
                    self._provider.revoke(token)
                except AppError:
                    pass
```

- [ ] **Step 3: verde verificado** — `test_privacy` + suite completa en verde. (Nota: con `EncryptedTokenStore` el `load` descifra en memoria; `delete` borra ciphertext: correcto.)

### Task 3: `backend/privacy.py` — orquestador `erase_installation`

**Files:**
- Create: `backend/privacy.py`
- Test: `backend/tests/test_privacy.py` (`EraseInstallationTest`: borrado total, ajenos intactos, idempotencia, revoke invocado, token compartido preservado+revocado)

**Interfaces:**
- Consumes: `ConnectService.erase_local`, `provider.revoke`, stores de Task 1.
- Produces: `erase_installation(installation_id, services, sessions, transactions, installations) -> dict` con forma `{"erased": {...}, "revoked": {...}}`. Usado por Task 4.

- [ ] **Step 1: test rojo — borrado total + ajenos intactos + idempotente**

```python
def test_erase_removes_own_data_keeps_others(self):
    # inst-a: youtube+kick conectados + 1 sesión + 1 transacción + installation
    # inst-b: youtube conectado (CUENTA DISTINTA) + 1 sesión + installation
    out = erase_installation("inst-a", services, sessions, transactions, installations)
    self.assertEqual(out["erased"], {"connections": 2, "tokens": 2, "sessions": 1,
                                     "transactions": 1, "installation": 1})
    self.assertTrue(all(out["revoked"].values()) and revoked_calls)
    # ajenos intactos
    self.assertEqual(services["youtube"].status("inst-b")["status"], "connected")
    # idempotente
    out2 = erase_installation("inst-a", services, sessions, transactions, installations)
    self.assertEqual(out2["erased"], {"connections": 0, "tokens": 0, "sessions": 0,
                                      "transactions": 0, "installation": 0})
```

(Expected: FAIL — `backend/privacy.py` no existe.)

- [ ] **Step 2: verde mínimo `backend/privacy.py`**

```python
"""Borrado total por instalación, F-C2 (T-064).

Orden F-030: DELETEs locales primero (con tokens copiados en memoria),
revoke remoto best-effort al final. Sin secretos en respuestas ni logs
(solo conteos por provider).
"""
from __future__ import annotations


def erase_installation(installation_id, services, sessions, transactions,
                       installations) -> dict:
    collected = {}  # provider -> (service, tokens|None)
    conns = toks = 0
    for name, service in services.items():
        entry, tokens = service.erase_local(installation_id)
        if entry is not None:
            conns += 1
        if tokens is not None:
            # ¿fila preservada por compartida? Releer: si load sigue
            # devolviendo, otra instalación la referencia.
            from backend.kernel import Account as _A  # ponytail: import local, evita ciclo
            acc = _A(provider=service._provider.provider,
                     provider_user_id=entry["account"]["provider_user_id"],
                     display_name=entry["account"]["display_name"],
                     scopes=tuple(entry["account"]["scopes"]))
            if service._tokens.load(acc) is None:
                toks += 1
        collected[name] = (service, tokens)
    sess_n = sessions.delete_for_installation(installation_id)
    txn_n = transactions.delete_for_installation(installation_id)
    installations.delete(installation_id)
    revoked = {}
    for name, (service, tokens) in collected.items():
        ok = True
        if tokens is not None:
            for token in (tokens.access_token, tokens.refresh_token):
                if not token:
                    continue
                try:
                    service._provider.revoke(token)
                except Exception:
                    ok = False  # best-effort; el borrado local ya ocurrió
        revoked[name] = ok
    return {"erased": {"connections": conns, "tokens": toks,
                       "sessions": sess_n, "transactions": txn_n,
                       "installation": 1},
            "revoked": revoked}
```

(Ajustar `installation` a 0 si `installations.load` previo era None: comprobar existencia antes de borrar. `except AppError` — no `Exception` genérico: coherente con `disconnect`. Refinar en verde.)

- [ ] **Step 3: test rojo — token compartido: fila preservada pero revoke invocado**

```python
def test_erase_shared_token_revokes_but_keeps_row(self):
    # inst-a e inst-b, MISMA cuenta UC1 (misma fila tokens)
    out = erase_installation("inst-a", ...)
    self.assertEqual(out["erased"]["tokens"], 0)   # compartida: fila queda
    self.assertTrue(revoked_calls)                  # pero revoke sí se intentó
    self.assertEqual(services["youtube"].status("inst-b")["status"], "connected")
```

- [ ] **Step 4: verde + suite completa.**

### Task 4: Ruta HTTP `POST /privacy/erase` + wiring

**Files:**
- Modify: `backend/http_server.py` (`BackendApp.__init__` + `_route_post`)
- Modify: `backend/app.py` (`create_app`: pasar `installations`/`transactions` a `BackendApp`)
- Test: `backend/tests/test_http.py` (añadir clase; mirar patrones existentes del fichero al implementar)

**Interfaces:**
- Consumes: `privacy.erase_installation` (Task 3).
- Produces: ruta `POST /privacy/erase` → 200 con el dict (sin secretos).

- [ ] **Step 1: test rojo (HTTP, con `create_app` + sesión real)**

```python
def test_privacy_erase_route(self):
    app = create_app(settings=Settings(host="127.0.0.1", port=0),
                     enable_youtube=True)  # stores InMemory cableados
    boot = app.auth.bootstrap()
    # firmar sesión como hace el plugin (sign_installation_secret) ...
    # POST /privacy/erase con bearer → 200, erased.installation == 1
    # segundo POST con el MISMO bearer → 401 (sesión purgada)
```

(Expected: FAIL — ruta desconocida.)

- [ ] **Step 2: verde mínimo** — en `_route_post`, antes de las rutas genéricas:

```python
if path == "/privacy/erase":
    record = app.check_access(path, self.headers)
    app._limited("auth_install", f"erase:{record.installation_id}")
    from backend import privacy as _privacy
    payload = _privacy.erase_installation(
        record.installation_id, app.providers, app.sessions,
        app.transactions, app.installations)
    app.log.info("privacy erase providers=%s erased=%s",
                 sorted(app.providers), payload["erased"],
                 extra={"requestId": request_id})
    return 200, payload
```

(`BackendApp.__init__`: nuevos kwargs `installations=None, transactions=None`; `create_app` los pasa en la rama de construcción propia; si `None` → `AppError(INTERNAL)` genérico sin detalle.)

- [ ] **Step 3: suite verde + leak-guard verde** (ningún secreto en el dict: solo ints/bools/nombres de provider).

### Task 5: C++ mínimo + NSIS

**Files:**
- Modify: `src/metadata_dock.h` (+ slot `onEraseManagedData()`), `src/metadata_dock.cpp` (botón "Borrar mis datos" en sección Managed + slot: `apiPost("/privacy/erase")` → snapshots Managed a disconnected + `backendInstall` local a disconnected + `saveStore()` + `setResult`)
- Modify: `cmake/windows-installer.nsi.in` (`un.onInit`: MessageBox informativo, sin abortar)
- Test: build + `metadata-selfcheck` en verde (sin checks nuevos: lógica Qt no unitaria; la evidencia es build+suite verde)

Pasos: (1) leer zona de botones Managed para colocar el botón (patrón existente, sin inventar layout); (2) implementar slot reutilizando `wipeLocal`/limpieza de snapshots de `onDisconnectManaged`; (3) compilar preset windows-x64; (4) selfcheck.

### Task 6: `PRIVACY.md` + BACKLOG

**Files:**
- Modify: `docs/PRIVACY.md` (§8 tabla de retención con plazos reales: sessions 30 min TTL + borrado en erase; transactions 600 s + borrado en erase; connections/tokens hasta Disconnect/erase; installation hasta erase; logs = default Cloud Run; §9: procedimiento de borrado — botón/endpoint/email; semántica revoke-por-provider + verificación — Google revoke invalida refresh aparejado, Twitch `GET /validate` → 401, Kick `POST /introspect` → `active:false`; §10: nota uninstaller; §14: contacto `horangelmillan@gmail.com`; Appendix: filas sessions/transactions/installation + columna de borrado vía erase).
- Modify: `docs/BACKLOG.md` (T-064 → en-progreso durante el trabajo; a hecha solo con evidencia y merge — sin commit aquí).

### Task 7: Puerta de evidencia

- [ ] `python -m unittest discover -s backend/tests` verde (0 skips nuevos salvo PG-sin-servidor por diseño).
- [ ] `metadata-selfcheck` compilado y verde.
- [ ] Estatal PG local (`initdb`, ver `DEPLOYMENT.md` §desarrollo): bootstrap → connect (fake o real según secreto) → `POST /privacy/erase` → `SELECT count(*)` = 0 en connections/tokens/sessions/transactions/installations → `/status` disconnected → reconnect exige OAuth nuevo. Salida con valores redactados.
- [ ] Secret-scan equivalente al CI (mirar `.github/workflows/ci-phase0.yml` y replicar el grep) en verde.
- [ ] Informe final (decisión, archivos, tests+salida, evidencia DB, git status, pendientes). SIN COMMIT.

## Self-Review (skill writing-plans)

1. **Spec coverage:** F-C2 (endpoint ✓ T3-4, botón ✓ T5, nota uninstaller ✓ T5, PRIVACY contacto/retención ✓ T6, PASS conectar→borrar→ceros→OAuth nuevo ✓ T7) · T-064 aceptación ✓ · investigación tokens-compartidos ✓ (guarded-delete, sin STOP: sin cambio arquitectónico) · orden F-030 ✓ · idempotencia ✓ · logs conteos ✓ · C++ mínimo ✓ · TDD rojo-primero ✓ (cada task empieza en rojo) · sin tocar OAuth/Apply/DCF ✓.
2. **Placeholder scan:** sin TBD/TODO; cada step trae código concreto y comando exacto; "mirar patrones existentes" solo donde el fichero manda (test_http.py, botones Managed) con instrucción de reutilizar, no de inventar.
3. **Type consistency:** `erase_local -> tuple[entry|None, TokenPair|None]`; `list_referencing -> list[str]`; `delete_for_installation -> int`; `erase_installation -> dict{erased, revoked}`; ruta devuelve ese dict. Nombres idénticos en todas las tasks.
