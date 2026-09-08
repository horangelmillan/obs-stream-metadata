#!/usr/bin/env python3
"""T-030: prueba viva de Kick con salida redactada (una sola orden).

Flujo OAuth 2.1 + PKCE (redirect localhost, secret SOLO local):
  state + PKCE -> navegador del sistema -> http://localhost:<puerto>/cb ->
  code (solo memoria) -> token exchange (con client_secret local) ->
  GET canal -> PATCH stream_title (204) -> read-back -> revoke (best effort).

NO imprime nunca: authorization code, code_verifier, client_secret,
access_token, refresh_token. Solo longitudes y datos operativos no
secretos (slug, stream_title, HTTP status).

El client_secret se obtiene (sin mostrarlo) por orden de preferencia:
  1. --client-secret <valor>
  2. env KICK_CLIENT_SECRET
  3. pregunta interactiva oculta (getpass)

Uso:
    python tools/t030_live_kick.py --client-id <ID> --title "T030 LIVE Kick <ts>"
    KICK_CLIENT_SECRET=<S> python tools/t030_live_kick.py --client-id <ID> --title "..."
    python tools/t030_live_kick.py --client-id <ID> --title "..." --restore "<original>"

El paso del navegador (autorizar en kick.com) sigue siendo manual:
OAuth exige consentimiento humano, no se puede automatizar.
"""

import argparse
import base64
import getpass
import hashlib
import json
import os
import secrets
import sys
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

AUTH_URL = "https://id.kick.com/oauth/authorize"
TOKEN_URL = "https://id.kick.com/oauth/token"
REVOKE_URL = "https://id.kick.com/oauth/revoke"
CHANNELS_URL = "https://api.kick.com/public/v1/channels"
SCOPES = "channel:write channel:read"


def b64url_sha256(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


def post_form(url, fields, token=None):
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300]


def api_get(url, token):
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:300]}


def api_patch(url, token, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="PATCH")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300]


def wait_for_code(port, expected_state, timeout=600):
    """localhost (NO 127.0.0.1, F-017): captura el code validando state."""
    result = {}
    done = threading.Event()

    class H(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            if not self.path.startswith("/cb"):
                self.send_response(404)
                self.end_headers()
                return
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
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

    srv = HTTPServer(("localhost", port), H)
    srv.timeout = 1
    t0 = time.time()
    while time.time() - t0 < timeout and not done.is_set():
        srv.handle_request()
    srv.server_close()
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Prueba viva Kick T-030 (redactada)")
    ap.add_argument("--client-id", required=True)
    ap.add_argument("--client-secret", default="",
                    help="si se omite: env KICK_CLIENT_SECRET o pregunta oculta")
    ap.add_argument("--title", required=True, help="titulo de prueba")
    ap.add_argument("--restore", default="", help="titulo original a restaurar")
    ap.add_argument("--port", type=int, default=3000)
    a = ap.parse_args()

    if not a.title:
        print("FAIL validacion local: titulo vacio")
        return 10

    secret = a.client_secret or os.environ.get("KICK_CLIENT_SECRET", "")
    if not secret:
        secret = getpass.getpass("client_secret de Kick (no se muestra): ").strip()
    if not secret:
        print("FAIL configuracion: sin client_secret (Kick lo exige, F-017)")
        return 11

    verifier = secrets.token_urlsafe(64)[:128]
    challenge = b64url_sha256(verifier)
    state = secrets.token_hex(16)
    redirect = f"http://localhost:{a.port}/cb"
    params = {"response_type": "code", "client_id": a.client_id,
              "redirect_uri": redirect, "scope": SCOPES, "state": state,
              "code_challenge": challenge, "code_challenge_method": "S256"}
    print(f"1. Abre en el navegador:\n{AUTH_URL}?{urllib.parse.urlencode(params)}")
    print(f"   (redirect exacto registrado: {redirect})")
    res = wait_for_code(a.port, state)
    if res.get("error"):
        print(f"FAIL oauth: proveedor devolvio error={res['error'][:100]}")
        return 21
    if res.get("state") != state or not res.get("code"):
        print("FAIL oauth: sin code o state no coincide (posible CSRF)")
        return 22
    code = res["code"]
    print("2. Callback OK (state coincide, code en memoria, longitud oculta)")

    s, b = post_form(TOKEN_URL, {"grant_type": "authorization_code",
                                 "client_id": a.client_id,
                                 "client_secret": secret,
                                 "redirect_uri": redirect,
                                 "code": code,
                                 "code_verifier": verifier})
    secret = ""  # fuera de memoria en cuanto deja de necesitarse
    if s != 200:
        print(f"FAIL token exchange: HTTP {s} {b[:200]}")
        return 23
    tok = json.loads(b)
    token, refresh = tok.get("access_token", ""), tok.get("refresh_token", "")
    if not token:
        print("FAIL token exchange: sin access_token en la respuesta")
        return 23
    print(f"3. Token OK (access {len(token)} chars"
          + (f", refresh {len(refresh)} chars" if refresh else ", sin refresh")
          + f", expira en {tok.get('expires_in')}s, scope={tok.get('scope', '')[:60]!r})")

    s, ch = api_get(CHANNELS_URL, token)
    items = ch.get("data", []) if s == 200 else []
    if s != 200 or not items:
        print(f"FAIL channel read: HTTP {s} {str(ch)[:200]}")
        return 24
    me = items[0]
    print(f"4. Canal: slug={me.get('slug', '')!r} "
          f"broadcaster_user_id={me.get('broadcaster_user_id', '')} "
          f"titulo original={me.get('stream_title', '')!r}")

    s, b = api_patch(CHANNELS_URL, token, {"stream_title": a.title})
    if s != 204:
        print(f"FAIL patch: HTTP {s} {b[:200]}")
        return 25
    print("5. PATCH 204 (sin body) OK")

    s, ch = api_get(CHANNELS_URL, token)
    items = ch.get("data", []) if s == 200 else []
    final = items[0].get("stream_title", "") if items else ""
    if s != 200 or final != a.title:
        print(f"FAIL read-back: HTTP {s}, stream_title={final!r}")
        return 26
    print(f"6. Read-back OK: {final!r} — verificalo en kick.com/{me.get('slug', '')}")

    if a.restore:
        s, _ = api_patch(CHANNELS_URL, token, {"stream_title": a.restore})
        print(f"7. Restore: HTTP {s}")

    for name, t in (("access", token), ("refresh", refresh)):
        if t:
            s, _ = post_form(REVOKE_URL, {"token": t})
            print(f"8. Revoke {name}: HTTP {s} (best effort)")
    print("KICK LIVE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
