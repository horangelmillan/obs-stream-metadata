#!/usr/bin/env python3
"""T-030: prueba viva de Twitch con salida redactada (una sola orden).

Automatiza lo repetible del device flow y NO muestra secretos:
- access_token / refresh_token / device_code: jamás se imprimen
  (solo longitudes y `expires_in`). Los tokens viven solo en memoria
  y se revocan al final (best effort).
- Lo visible (user_code, login, user_id, títulos, HTTP status) es
  material operativo, no secreto.

Uso:
    python tools/t030_live_twitch.py --client-id <ID> --title "T030 LIVE Twitch <ts>"
    python tools/t030_live_twitch.py --client-id <ID> --title "..." --restore "<título original>"

El paso del navegador (activate + user_code) sigue siendo manual:
OAuth exige consentimiento humano, no se puede automatizar.
"""
import argparse
import json
import sys
import time
import urllib.parse
import urllib.request

DEVICE_URL = "https://id.twitch.tv/oauth2/device"
TOKEN_URL = "https://id.twitch.tv/oauth2/token"
VALIDATE_URL = "https://id.twitch.tv/oauth2/validate"
REVOKE_URL = "https://id.twitch.tv/oauth2/revoke"
CHANNELS_URL = "https://api.twitch.tv/helix/channels"
SCOPE = "channel:manage:broadcast"


def post_form(url, fields, token=None, client_id=None):
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    if client_id:
        req.add_header("Client-Id", client_id)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def get_json(url, token, client_id):
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Client-Id", client_id)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read().decode("utf-8", "replace")[:200]}


def patch_title(client_id, token, broadcaster_id, title):
    url = CHANNELS_URL + "?broadcaster_id=" + urllib.parse.quote(broadcaster_id)
    body = json.dumps({"title": title}).encode()
    req = urllib.request.Request(url, data=body, method="PATCH")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Client-Id", client_id)
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300]


def main() -> int:
    ap = argparse.ArgumentParser(description="Prueba viva Twitch T-030 (redactada)")
    ap.add_argument("--client-id", required=True)
    ap.add_argument("--title", required=True, help="título de prueba")
    ap.add_argument("--restore", default="", help="título original a restaurar")
    ap.add_argument("--interval", type=int, default=5)
    ap.add_argument("--timeout", type=int, default=600)
    a = ap.parse_args()

    if len(a.title) > 140 or not a.title:
        print("FAIL validación local: título vacío o >140")
        return 10

    s, b = post_form(DEVICE_URL, {"client_id": a.client_id, "scopes": SCOPE})
    if s != 200:
        print(f"FAIL device start: HTTP {s} {b[:200]}")
        return 11
    d = json.loads(b)
    print(f"1. Abre {d['verification_uri']} e introduce user_code: {d['user_code']}")
    input("2. Pulsa Enter cuando hayas autorizado... ")

    token, refresh = None, None
    t0 = time.time()
    while time.time() - t0 < a.timeout:
        s, b = post_form(TOKEN_URL, {
            "client_id": a.client_id, "scopes": SCOPE,
            "device_code": d["device_code"],
            "grant_type": "urn:ietf:params:oauth:grant-type:device_code"})
        if s == 200:
            j = json.loads(b)
            token, refresh = j["access_token"], j.get("refresh_token", "")
            print(f"3. Token OK (access {len(token)} chars, expira en {j.get('expires_in')}s)")
            break
        time.sleep(a.interval)
    if not token:
        print(f"FAIL token poll: último HTTP {s} {b[:200]}")
        return 12

    s, v = get_json(VALIDATE_URL, token, a.client_id)
    if s != 200 or SCOPE not in v.get("scopes", []):
        print(f"FAIL validate/scope: HTTP {s}")
        return 13
    bid = str(v["user_id"])
    print(f"4. Identidad: login={v['login']} user_id={bid} scopes={v['scopes']} (broadcaster_id OK)")

    s, ch = get_json(CHANNELS_URL + "?broadcaster_id=" + bid, token, a.client_id)
    if s != 200:
        print(f"FAIL channel read: HTTP {s}")
        return 14
    print(f"5. Título original: {ch['data'][0]['title']}")

    s, b = patch_title(a.client_id, token, bid, a.title)
    if s != 204:
        print(f"FAIL patch: HTTP {s} {b[:200]}")
        return 15
    print("6. PATCH 204 (sin body) OK")

    s, ch = get_json(CHANNELS_URL + "?broadcaster_id=" + bid, token, a.client_id)
    final = ch["data"][0]["title"] if s == 200 else ""
    if final != a.title:
        print(f"FAIL read-back: HTTP {s}, título={final!r}")
        return 16
    print(f"7. Read-back OK: {final!r} — compruébalo también en twitch.tv/{v['login']}")

    if a.restore:
        s, _ = patch_title(a.client_id, token, bid, a.restore)
        print(f"8. Restore: HTTP {s}")

    for name, tok in (("access", token), ("refresh", refresh)):
        if tok:
            s, _ = post_form(REVOKE_URL + f"?client_id={a.client_id}&token={tok}", {})
            print(f"9. Revoke {name}: HTTP {s}")
    print("TWITCH LIVE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
