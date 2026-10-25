"""T-078 RelayFmt: prueba de formatos dest forward->Meta (throwaway, solo lab).

Crea UN LiveVideo "RelayFmt T-078" (UNPUBLISHED/EVERYONE) via lab :3002,
prueba 3 variantes de dest contra MediaMTX :9997/path live (fuente activa
del operador, no se toca), y limpia (forward [] + DELETE propio).

REGLA DURA: jamas imprime token/code/ingesta/clave/URL con valores.
Solo: IDs, formas (scheme/host/puerto/split), state, bytes>0, status.
"""
import json
import time
import urllib.request
from urllib.parse import urlsplit

MTX = "http://127.0.0.1:9997"
LAB = "http://localhost:3002"
PATH = "live"
TITLE = "RelayFmt T-078"


def call(base, method, path, body=None, timeout=15):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(
        base + path, data=data, method=method,
        headers={"User-Agent": "t078-lab", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8")
    except Exception as e:
        return "ERROR", str(e)[:200]


def lab_graph(method, path, body=None):
    st, raw = call(LAB, "POST", "/api/graph",
                   {"method": method, "path": path, **({"body": body} if body is not None else {})})
    assert st == 200, ("lab-graph-http", st, raw[:120])
    payload = json.loads(raw)
    assert payload.get("ok"), ("lab-graph-nok", json.dumps(payload)[:120])
    return payload["data"]


def shape_of_url(u):
    """Descriptor de forma SIN valores: scheme, puerto, nº segmentos, lens."""
    p = urlsplit(u)
    segs = [s for s in p.path.split("/") if s]
    return {"scheme": p.scheme, "has_port": bool(p.port),
            "path_segs": len(segs),
            "last_seg_len": len(segs[-1]) if segs else 0,
            "has_query": bool(p.query), "query_len": len(p.query)}


def sanitize(text, secrets):
    out = str(text)
    for s in secrets:
        if s and len(s) >= 4:
            out = out.replace(s, "***")
    # recorta y elimina restos con pinta de URL con credencial
    return out[:90]


def err_class(text):
    t = str(text).lower()
    for k in ["timeout", "eof", "refused", "handshake", "tls", "certificate",
              "no such host", "not found", "unauthorized", "forbidden",
              "closed", "reset", "unreachable"]:
        if k in t:
            return k
    return "other"


def set_forward(dest_list):
    st, raw = call(MTX, "PATCH", "/v3/config/paths/patch/" + PATH,
                   {"forward": dest_list})
    assert st == 200, ("patch-http", st, raw[:120])
    # El PATCH responde {"status":...}; el estado real se lee con GET.
    st2, raw2 = call(MTX, "GET", "/v3/config/paths/get/" + PATH)
    assert st2 == 200, ("get-conf-http", st2, raw2[:120])
    conf = json.loads(raw2)
    return len(conf.get("forward") or [])


def poll_dest(wait_s=25):
    """Devuelve (state, bytes_gt0, errclass, err_sanitized_len)."""
    state, ob, ec = "?", False, "?"
    esnip = ""
    t0 = time.time()
    while time.time() - t0 < wait_s:
        st, raw = call(MTX, "GET", "/v3/paths/forward-dests/list?path=" + PATH)
        if st == 200:
            items = json.loads(raw).get("items") or []
            if items:
                d = items[0]
                state = d.get("state")
                ob = (d.get("outboundBytes") or 0) > 0
                if d.get("lastError"):
                    ec = err_class(d["lastError"])
                    esnip = d["lastError"]
                if state == "forwarding" and ob:
                    break
        time.sleep(5)
    return state, ob, ec, esnip


def main():
    secrets = []
    # 0) estado lab
    st, raw = call(LAB, "GET", "/api/state")
    s0 = json.loads(raw)
    print("lab connected:", s0.get("connected"))

    # 1) create
    data = lab_graph("POST", "/me/live_videos", {
        "_lab_create": True, "title": TITLE,
        "description": "T-078 prueba formato forward (throwaway)",
        "status": "UNPUBLISHED", "privacy": {"value": "EVERYONE"}})
    live_id = str(data.get("id"))
    sec_url = str(data.get("secure_stream_url") or "")
    rtmp_url = str(data.get("stream_url") or "")
    assert live_id and sec_url, "create sin id/ingesta"
    secrets += [live_id, sec_url, rtmp_url]
    # la clave es el ultimo segmento: tambien secreto
    try:
        key0 = urlsplit(sec_url).path.rsplit("/", 1)[-1]
        secrets.append(key0)
    except Exception:
        pass
    print("created id:", live_id)
    print("secure_stream_url shape:", json.dumps(shape_of_url(sec_url)))
    print("stream_url present:", bool(rtmp_url),
          "shape:", json.dumps(shape_of_url(rtmp_url)) if rtmp_url else "{}")

    # 2) construir variantes (solo se describe la forma)
    p = urlsplit(sec_url)
    base = f"{p.scheme}://{p.hostname}" + (f":{p.port}" if p.port else "") + \
        p.path.rsplit("/", 1)[0]
    key = p.path.rsplit("/", 1)[-1] + (("?" + p.query) if p.query else "")
    variants = [
        ("a-integra", sec_url, "secure_stream_url tal cual, sin '#'"),
        ("b-split-hash", base + "#" + key,
         "base(rtmps/host:port/path-sin-key) + '#' + key; port_present=%s" % bool(p.port)),
    ]
    if rtmp_url:
        variants.append(("c-rtmp-integra", rtmp_url, "stream_url rtmp tal cual"))
    print("variantes (forma, sin valores):", [(v[0], v[2]) for v in variants])

    results = []
    for name, dest, _desc in variants:
        n = set_forward([{"dest": dest}])
        assert n == 1, ("forward-count", n)
        state, ob, ec, esnip = poll_dest()
        try:
            meta = lab_graph("GET", f"/{live_id}?fields=id,status")
            mstatus = meta.get("status")
        except Exception as e:
            mstatus = "read-err:" + str(e)[:40]
        print(f"var {name}: forwarder_state={state} outboundBytes_gt0={ob} "
              f"lasterr_class={ec} meta_status={mstatus}")
        if esnip:
            print(f"  lasterr(redacted): {sanitize(esnip, secrets)}")
        results.append((name, state, ob, ec, mstatus))

    # 3) limpieza: forward [] + DELETE propio
    n = set_forward([])
    print("forward revert count:", n)
    st, raw = call(LAB, "POST", "/api/graph",
                   {"method": "DELETE", "path": f"/{live_id}"})
    print("delete propio:", st, raw[:120])
    st, raw = call(MTX, "GET", "/v3/paths/forward-dests/list?path=" + PATH)
    print("forward-dests final:", json.loads(raw).get("itemCount"))
    print("RESULT:", [(r[0], r[1], r[2], r[3], r[4]) for r in results])
    print("NOTA: sin GoLive (exige OK explicito separado).")


if __name__ == "__main__":
    main()
