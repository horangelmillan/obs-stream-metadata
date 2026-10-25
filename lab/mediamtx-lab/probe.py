import urllib.request, json

BASE = "http://127.0.0.1:9997"

def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"User-Agent": "t078-lab",
                                          "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8")[:800]
    except Exception as e:
        return "ERROR", str(e)[:300]

# (i) buscar OpenAPI local / docs embebidas
for p in ["/v3/openapi.json", "/v3/openapi.yaml", "/openapi.json",
          "/v3/docs", "/v3/swagger.json"]:
    print("GET", p, "->", call("GET", p))

# (ii) candidatos reload / config global
for m, p in [("POST", "/v3/config/reload"), ("GET", "/v3/config/reload"),
             ("POST", "/v3/config/set"), ("GET", "/v3/config"),
             ("POST", "/v3/restart"), ("GET", "/v3/auth/jwks/refresh")]:
    print(m, p, "->", call(m, p))
