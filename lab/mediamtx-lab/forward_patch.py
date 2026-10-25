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

def get_forward():
    st, body = call("GET", "/v3/config/paths/get/live")
    assert st == 200, (st, body)
    return json.loads(body).get("forward")

print("forward inicial:", get_forward())

# 1) PATCH parcial con destino FICTICIO (NO Meta real)
st, body = call("PATCH", "/v3/config/paths/patch/live",
                {"forward": [{"dest": "rtmps://ejemplo:1935/x#k"}]})
print("PATCH ficticio ->", st, body[:300])
print("forward tras PATCH:", get_forward())

# 2) revertir a vacío (dejar estado limpio)
st, body = call("PATCH", "/v3/config/paths/patch/live", {"forward": []})
print("PATCH revert ->", st, body[:300])
print("forward final:", get_forward())
