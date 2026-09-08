#!/usr/bin/env python3
"""T-030: prueba viva de YouTube con salida redactada (una sola orden).

Flujo installed-app + PKCE (cliente Desktop; Google exige client_secret
local en el intercambio desde ~2025 aunque sea Desktop — se pide por
--client-secret / env GOOGLE_CLIENT_SECRET / pregunta oculta, nunca se
imprime ni se commitea):
  state + PKCE -> navegador del sistema -> loopback local ->
  code (solo memoria) -> token exchange -> listar broadcasts ->
  GET recurso -> PUT liveBroadcasts.update (solo snippet.title) ->
  read-back -> revoke (best effort).

NO imprime nunca: authorization code, code_verifier, access_token,
refresh_token. Solo longitudes y datos operativos no secretos
(broadcast id/titulo/estado).

Uso:
    python tools/t030_live_youtube.py --client-id <ID> --title "T030 LIVE YouTube <ts>"
    python tools/t030_live_youtube.py --client-id <ID> --title "..." --restore "<titulo original>"
    python tools/t030_live_youtube.py --client-id <ID> --title "..." --broadcast-id <ID>

El paso del navegador (consentimiento Google) sigue siendo manual:
OAuth exige consentimiento humano, no se puede automatizar.
NO toca la descripcion: solo modifica snippet.title (resto preservado).
"""

import argparse
import getpass
import json
import os
import secrets
import sys
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_URL = "https://oauth2.googleapis.com/token"
REVOKE_URL = "https://oauth2.googleapis.com/revoke"
LIST_URL = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
UPDATE_URL = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
SCOPE = "https://www.googleapis.com/auth/youtube.force-ssl"


def b64url_sha256(verifier: str) -> str:
    import base64
    import hashlib

    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


def api_get(url, token):
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:300]}


def api_put(url, token, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="PUT")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:300]}


def wait_for_code(port, expected_state, timeout=600):
    """Loopback 127.0.0.1: captura el code validando state. Ignora favicon."""
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

    srv = HTTPServer(("127.0.0.1", port), H)
    srv.timeout = 1
    t0 = time.time()
    while time.time() - t0 < timeout and not done.is_set():
        srv.handle_request()
    srv.server_close()
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Prueba viva YouTube T-030 (redactada)")
    ap.add_argument("--client-id", required=True)
    ap.add_argument("--client-secret", default="",
                    help="si se omite: env GOOGLE_CLIENT_SECRET o pregunta "
                    "oculta (Google lo exige incluso en Desktop, 2025+)")
    ap.add_argument("--title", required=True, help="titulo de prueba")
    ap.add_argument("--restore", default="", help="titulo original a restaurar")
    ap.add_argument("--port", type=int, default=9004)
    ap.add_argument("--broadcast-id", default="",
                    help="broadcast a usar (si se omite, se lista y se elige)")
    a = ap.parse_args()

    if not a.title or len(a.title) > 100:
        print("FAIL validacion local: titulo vacio o >100")
        return 10

    verifier = secrets.token_urlsafe(64)[:128]
    challenge = b64url_sha256(verifier)
    state = secrets.token_hex(16)
    redirect = f"http://127.0.0.1:{a.port}/"
    params = {
        "response_type": "code", "client_id": a.client_id,
        "redirect_uri": redirect, "scope": SCOPE, "state": state,
        "code_challenge": challenge, "code_challenge_method": "S256",
        "access_type": "offline", "prompt": "consent",
    }
    print(f"1. Abre en el navegador:\n{AUTH_URL}?{urllib.parse.urlencode(params)}")
    print(f"   (redirect loopback {redirect}; espera el callback local)")
    res = wait_for_code(a.port, state)
    if res.get("error"):
        print(f"FAIL oauth: proveedor devolvio error={res['error'][:100]}")
        return 21
    if res.get("state") != state or not res.get("code"):
        print("FAIL oauth: sin code o state no coincide (posible CSRF)")
        return 22
    code = res["code"]
    print("2. Callback OK (state coincide, code en memoria, longitud oculta)")

    secret = (a.client_secret or os.environ.get("GOOGLE_CLIENT_SECRET", ""))
    if not secret:
        secret = getpass.getpass(
            "client_secret Google (Desktop: no es confidencial, no se muestra): "
        ).strip()
    if not secret:
        print("FAIL configuracion: Google exige client_secret en el "
              "intercambio (incluso Desktop)")
        return 11
    fields = {"grant_type": "authorization_code", "code": code,
              "client_id": a.client_id, "client_secret": secret,
              "redirect_uri": redirect, "code_verifier": verifier}
    secret = ""
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(TOKEN_URL, data=data, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            tok = json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        print(f"FAIL token exchange: HTTP {e.code} "
              f"{e.read().decode('utf-8', 'replace')[:200]}")
        return 23
    token, refresh = tok["access_token"], tok.get("refresh_token", "")
    print(f"3. Token OK (access {len(token)} chars"
          + (f", refresh {len(refresh)} chars" if refresh else ", sin refresh") + ")")
    if SCOPE not in tok.get("scope", ""):
        print(f"FAIL scope: concedido={tok.get('scope', '')[:120]!r}")
        return 24

    bid = a.broadcast_id
    if not bid:
        # La API exige exactamente UN filtro (broadcastStatus|id|mine):
        # mine=true solo devuelve los propios (default: todos los estados);
        # el filtrado active/upcoming se hace en local por lifeCycleStatus.
        q = urllib.parse.urlencode({"part": "id,snippet,status",
                                    "mine": "true",
                                    "maxResults": "50"})
        s, lst = api_get(f"{LIST_URL}?{q}", token)
        if s != 200:
            print(f"FAIL list mine: HTTP {s} {str(lst)[:200]}")
            return 25
        found = []
        for it in lst.get("items", []):
            sn, st = it.get("snippet", {}), it.get("status", {})
            found.append((it["id"], sn.get("title", ""),
                          st.get("lifeCycleStatus", "")))
        if not found:
            print("FAIL broadcast_selection: sin broadcasts propios "
                  "(crea uno en YouTube Studio primero)")
            return 26
        print("4. Broadcasts (mine, active/upcoming):")
        for i, (i_id, i_t, i_s) in enumerate(found):
            print(f"   [{i}] id={i_id} status={i_s} title={i_t!r}")
        if len(found) == 1:
            bid = found[0][0]
        else:
            pick = input(f"   Elige indice [0-{len(found) - 1}]: ").strip()
            try:
                bid = found[int(pick)][0]
            except (ValueError, IndexError):
                print("FAIL broadcast_selection: indice invalido")
                return 26
    print(f"5. Broadcast elegido: id={bid}")

    q = urllib.parse.urlencode({"part": "id,snippet,contentDetails,status",
                                "id": bid})
    s, cur = api_get(f"{LIST_URL}?{q}", token)
    items = cur.get("items", []) if s == 200 else []
    if s != 200 or not items:
        print(f"FAIL broadcast get: HTTP {s} {str(cur)[:200]}")
        return 27
    full, sn = items[0], items[0].get("snippet", {})
    print(f"6. Recurso actual: title={sn.get('title', '')!r} "
          f"lifeCycle={items[0].get('status', {}).get('lifeCycleStatus', '')}")

    # part=snippet solo: el body NO puede incluir contentDetails
    # (la API lo rechaza con unexpectedPart). Solo snippet (completo,
    # con title sustituido) + id; el resto de parts queda intacto.
    put_body = {"id": bid, "snippet": dict(sn, title=a.title)}
    s, upd = api_put(f"{UPDATE_URL}?part=snippet", token, put_body)
    if s != 200:
        print(f"FAIL update: HTTP {s} {str(upd)[:300]}")
        return 28
    print("7. PUT update 200 OK")

    s, back = api_get(f"{LIST_URL}?{q}", token)
    bitems = back.get("items", []) if s == 200 else []
    final = bitems[0].get("snippet", {}).get("title", "") if bitems else ""
    if final != a.title:
        print(f"FAIL read-back: HTTP {s}, title={final!r}")
        return 29
    print(f"8. Read-back OK: {final!r} — verificalo en YouTube Studio")

    if a.restore:
        if not a.restore or len(a.restore) > 100:
            print("8b. Restore omitido: titulo original invalido (>100/vacio)")
        else:
            rb = {"id": bid, "snippet": dict(sn, title=a.restore)}
            s, _ = api_put(f"{UPDATE_URL}?part=snippet", token, rb)
            print(f"8b. Restore: HTTP {s}")

    for name, t in (("access", token), ("refresh", refresh)):
        if t:
            try:
                d = urllib.parse.urlencode({"token": t}).encode()
                rq = urllib.request.Request(REVOKE_URL, data=d, method="POST")
                with urllib.request.urlopen(rq, timeout=30) as r:
                    print(f"9. Revoke {name}: HTTP {r.status}")
            except urllib.error.HTTPError as e:
                print(f"9. Revoke {name}: HTTP {e.code} (best effort)")
    print("YOUTUBE LIVE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
