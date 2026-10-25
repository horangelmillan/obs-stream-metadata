import urllib.request, json

BASE = "http://127.0.0.1:9997"

def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"User-Agent": "t078-lab",
                                          "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8")
    except Exception as e:
        return "ERROR", str(e)[:200]

# forward-dests sin stream (debe estar vacío pero 200)
for p in ["/v3/paths/forward-dests/list", "/v3/paths/get/live",
          "/v3/rtmp/conns/list", "/v3/info"]:
    print(call("GET", p)[0], p, "->", call("GET", p)[1][:300])
