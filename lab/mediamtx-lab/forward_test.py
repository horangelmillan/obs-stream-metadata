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
        return "ERROR", str(e)[:300]

# 1) leer conf actual del path live
st, body = call("GET", "/v3/config/paths/get/live")
print("GET conf ->", st)
conf = json.loads(body)
print("forward actual:", conf.get("forward"))

# 2) PATCH con destino FICTICIO (NO Meta real)
conf["forward"] = [{"dest": "rtmps://ejemplo:1935/x#k"}]
st, body = call("PATCH", "/v3/config/paths/update/live", conf)
print("PATCH forward ficticio ->", st, body[:500])

# 3) read-back
st, body = call("GET", "/v3/config/paths/get/live")
print("GET read-back ->", st)
print("forward ahora:", json.loads(body).get("forward"))
