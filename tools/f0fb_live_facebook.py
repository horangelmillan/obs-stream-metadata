#!/usr/bin/env python3
"""FB-1: sondas vivas manuales Facebook (T-070, estilo tools/t030_live_*.py).

Sondas del brief docs/T0FB-RESEARCH.md §9 + hipotesis H1 (solo-lectura):
  A. H1 list: con ingesta externa en vista previa, GET /{user|page}/live_videos
     + comparar contra el ID de la URL de Live Producer + GET status/title.
     Esperado: el video aparece con status UNPUBLISHED. Si H1 falla -> parar,
     registrar en FINDINGS, preguntar (no redisenar en esta sesion).
  B. update en vista previa: POST /{live-video-id} titulo+desc (+privacy
     EVERYONE, D11) + read-back GET ?fields=status,title,description.
  C. go-live LIVE_NOW: SOLO con aprobacion explicita del operador
     (--probe golive + confirmacion interactiva). Sin flag: el runner se niega.

Graph API v25/v26; app manage-streams en Development + test users.
Jamas publish_to_groups/email. Cuenta real solo donde el brief lo permite
(perfil E2E D9; Page real pendiente de Page 100+).

Secretos SOLO memoria/entorno; revoke al final (DELETE /{user-id}/permissions,
best effort). NO imprime nunca: code, app_secret, access_token.
Solo longitudes + datos operativos no secretos (ids/titulos/status).

Uso tipico (operador):
  python tools/f0fb_live_facebook.py --app-id <ID> --probe list-only
  python tools/f0fb_live_facebook.py --app-id <ID> --probe update --target-mode profile --title "FB-1 <ts>" --desc "..." [--live-video-id 123]
  python tools/f0fb_live_facebook.py --app-id <ID> --probe golive --live-video-id 123  # pide confirmacion

OAuth manual desktop (brief §1): el runner imprime la URL dialog/oauth
(state+redirect localhost dev), el operador autoriza en el navegador y pega
el code en el loopback local (igual que t030_oauth_callback.py).
"""

import argparse
import base64
import hashlib
import json
import secrets
import sys
import threading
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

GRAPH = "https://graph.facebook.com/v26.0"
AUTH_URL = "https://www.facebook.com/v26.0/dialog/oauth"
TOKEN_URL = f"{GRAPH}/oauth/access_token"


def redacted_get(url, token):
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:400]}


def redacted_post(url, token, params):
    data = urllib.parse.urlencode(params).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8")
            return r.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:400]}


def wait_for_code(port, expected_state, timeout=600):
    result = {}
    done = threading.Event()

    class H(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            if "code" not in q and "error" not in q:
                self.send_response(404)
                self.end_headers()
                return
            result["code"] = (q.get("code") or [""])[0]
            result["state"] = (q.get("state") or [""])[0]
            result["error"] = (q.get("error") or [""])[0]
            ok = result["state"] == expected_state and bool(result["code"])
            body = ("OK: vuelve a la terminal. Puedes cerrar esta pestana."
                    if ok else "ERROR: state o code invalidos.").encode()
            self.send_response(200 if ok else 400)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            if ok or result.get("error"):
                done.set()

        def log_message(self, *a):
            pass

    import time
    srv = HTTPServer(("127.0.0.1", port), H)
    srv.timeout = 1
    t0 = time.time()
    while time.time() - t0 < timeout and not done.is_set():
        srv.handle_request()
    srv.server_close()
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Sondas vivas Facebook FB-1 (redactadas)")
    ap.add_argument("--app-id", required=True)
    ap.add_argument("--app-secret", default="",
                    help="LEGADO ignorado: PKCE sin secret en apps nativas (F-065)")
    ap.add_argument("--probe", required=True, choices=["list-only", "update", "golive"])
    ap.add_argument("--target-mode", default="profile", choices=["profile", "page"])
    ap.add_argument("--page-id", default="")
    ap.add_argument("--title", default="")
    ap.add_argument("--desc", default="")
    ap.add_argument("--live-video-id", default="")
    ap.add_argument("--producer-id", default="",
                    help="ID de la URL de Live Producer para comparar H1")
    ap.add_argument("--port", type=int, default=9009)
    ap.add_argument("--i-understand-live", action="store_true",
                    help="requerido para --probe golive")
    a = ap.parse_args()

    if a.probe in ("update",) and (not a.title or len(a.title) > 254):
        print("FAIL validacion local: titulo requerido 1-254 (D2)")
        return 10
    if a.probe == "golive" and not a.i_understand_live:
        print("REFUSE golive: requiere aprobacion explicita del operador "
              "(--i-understand-live + confirmacion interactiva). Sonda C bloqueada por diseno.")
        return 11
    if a.target_mode == "page" and not a.page_id:
        print("FAIL configuracion: --target-mode page exige --page-id")
        return 12

    scope = "publish_video" if a.target_mode == "profile" else \
        "pages_manage_posts,pages_read_engagement,pages_show_list"
    if "publish_to_groups" in scope or "email" in scope.split(","):
        print("FAIL scopes: jamas publish_to_groups/email")
        return 13

    state = secrets.token_hex(16)
    # PKCE nativo (F-065/F-067: la app es Nativa/Desktop y Meta rechaza el
    # exchange con client_secret → 400 "configured as a desktop app").
    raw = secrets.token_bytes(64)
    verifier = base64.urlsafe_b64encode(raw).rstrip(b"=").decode()
    challenge = base64.urlsafe_b64encode(
        hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    redirect = f"http://localhost:{a.port}/cb"
    params = {"client_id": a.app_id, "redirect_uri": redirect,
              "state": state, "scope": scope, "response_type": "code",
              "code_challenge": challenge,
              "code_challenge_method": "S256"}
    print(f"1. Abre en el navegador:\n{AUTH_URL}?{urllib.parse.urlencode(params)}")
    print(f"   (app manage-streams en Development; redirect {redirect})")
    res = wait_for_code(a.port, state)
    if res.get("error"):
        print(f"FAIL oauth: proveedor devolvio error={res['error'][:100]}")
        return 21
    if res.get("state") != state or not res.get("code"):
        print("FAIL oauth: sin code o state no coincide (posible CSRF)")
        return 22
    code = res["code"]
    print("2. Callback OK (state coincide, code en memoria, longitud oculta)")

    fields = {"client_id": a.app_id,
              "redirect_uri": redirect, "code": code,
              "code_verifier": verifier}
    code = ""
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(TOKEN_URL, data=data, method="GET")
    # exchange via GET con query (documentado Meta); reconstruir URL:
    req = urllib.request.Request(TOKEN_URL + "?" + urllib.parse.urlencode(fields), method="GET")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            tok = json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        print(f"FAIL token exchange: HTTP {e.code} {e.read().decode('utf-8', 'replace')[:200]}")
        return 23
    if "error" in tok:
        print(f"FAIL token exchange: {str(tok['error'])[:200]}")
        return 23
    token = tok.get("access_token", "")
    print(f"3. Token OK (access {len(token)} chars, tipo={tok.get('token_type', '?')})")

    owner = "me" if a.target_mode == "profile" else a.page_id
    s, lst = redacted_get(
        f"{GRAPH}/{owner}/live_videos?fields=id,title,description,status", token)
    if s != 200:
        print(f"FAIL list live_videos: HTTP {s} {str(lst)[:300]}")
        return 25
    items = lst.get("data", [])
    print(f"4. live_videos ({len(items)}):")
    for it in items:
        print(f"   id={it.get('id')} status={it.get('status')} title={it.get('title', '')!r}")
    if a.producer_id:
        match = [it for it in items if str(it.get("id")) == str(a.producer_id)]
        print(f"5. H1 compare producer-id={a.producer_id}: "
              + ("MATCH (H1 sostiene)" if match else "SIN MATCH (H1 en riesgo: parar y registrar)"))
        if match:
            print(f"   status={match[0].get('status')} (esperado UNPUBLISHED en vista previa)")
    if a.probe == "list-only":
        print("LIST-ONLY: PASS (sin escrituras)")
        return 0

    bid = a.live_video_id or (items[0].get("id") if items else "")
    if not bid:
        print("FAIL seleccion: sin live-video (indica --live-video-id o crea uno programado)")
        return 26
    s, cur = redacted_get(f"{GRAPH}/{bid}?fields=id,status,title,description", token)
    if s != 200:
        print(f"FAIL read: HTTP {s} {str(cur)[:300]}")
        return 27
    print(f"6. Actual: status={cur.get('status')} title={cur.get('title', '')!r}")

    if a.probe == "update":
        params_u = {"title": a.title}
        if a.desc:
            params_u["description"] = a.desc
        s, upd = redacted_post(f"{GRAPH}/{bid}", token, params_u)
        if s != 200:
            print(f"FAIL update: HTTP {s} {str(upd)[:400]}")
            return 28
        print("7. POST update 200 OK")
        s, back = redacted_get(f"{GRAPH}/{bid}?fields=id,status,title,description", token)
        ok = (s == 200 and back.get("title") == a.title
              and (not a.desc or back.get("description") == a.desc))
        if not ok:
            print(f"FAIL read-back: HTTP {s} title={str(back.get('title'))[:80]!r}")
            return 29
        print(f"8. Read-back OK title={back.get('title')!r} status={back.get('status')} — verificar en Live Producer")
        print("UPDATE: PASS (revoke pendiente)")
    elif a.probe == "golive":
        confirm = input(f"   CONFIRMAR go-live de {bid} a LIVE_NOW [escribe SI]: ").strip()
        if confirm != "SI":
            print("ABORT golive: sin confirmacion explicita")
            return 30
        s, upd = redacted_post(f"{GRAPH}/{bid}", token, {"status": "LIVE_NOW"})
        if s != 200:
            print(f"FAIL golive: HTTP {s} {str(upd)[:400]}")
            return 31
        print("7. POST status=LIVE_NOW 200 OK — verificar en Facebook")
        print("GOLIVE: PASS (revoke pendiente)")

    # Revoke best-effort: DELETE /{user-id}/permissions (brief §1).
    try:
        s, me = redacted_get(f"{GRAPH}/me?fields=id", token)
        uid = me.get("id", "") if s == 200 else ""
        if uid:
            req = urllib.request.Request(f"{GRAPH}/{uid}/permissions", method="DELETE")
            req.add_header("Authorization", "Bearer " + token)
            try:
                with urllib.request.urlopen(req, timeout=30) as r:
                    print(f"9. Revoke DELETE /permissions: HTTP {r.status} (best effort)")
            except urllib.error.HTTPError as e:
                print(f"9. Revoke DELETE /permissions: HTTP {e.code} (best effort)")
    except Exception as e:  # noqa: BLE001 - best effort
        print(f"9. Revoke omitido (best effort): {str(e)[:100]}")
    print("FB-1 RUNNER: DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
