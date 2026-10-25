import urllib.request, socket, json

# 1) puerto RTMP :1935 escuchando
s = socket.create_connection(("127.0.0.1", 1935), timeout=5)
print("RTMP :1935 TCP connect OK")
s.close()

# 2) Control API paths/list
req = urllib.request.Request("http://127.0.0.1:9997/v3/paths/list",
                             headers={"User-Agent": "t078-lab"})
with urllib.request.urlopen(req, timeout=10) as r:
    body = r.read().decode("utf-8")
    print("GET /v3/paths/list ->", r.status)
    print("body:", body[:2000])

# 3) explorar API: endpoints de config disponibles
for path in ["/v3/config/get", "/v3/config/paths/list",
             "/v3/config/paths/get/live", "/v3/info"]:
    try:
        req = urllib.request.Request("http://127.0.0.1:9997" + path,
                                     headers={"User-Agent": "t078-lab"})
        with urllib.request.urlopen(req, timeout=10) as r:
            print(path, "->", r.status, r.read().decode("utf-8")[:600])
    except Exception as e:
        print(path, "ERROR:", e)
