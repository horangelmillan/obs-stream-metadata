import urllib.request
url = "https://raw.githubusercontent.com/bluenviron/mediamtx/v1.21.3/api/openapi.yaml"
req = urllib.request.Request(url, headers={"User-Agent": "t078-lab"})
with urllib.request.urlopen(req, timeout=30) as r:
    data = r.read().decode("utf-8")
open("lab/mediamtx-lab/downloads/openapi-v1.21.3.yaml", "w", encoding="utf-8").write(data)
print("bytes:", len(data))
# imprimir solo paths (rutas) y métodos
import re
cur = None
for line in data.splitlines():
    m = re.match(r"^  (/[^ :]*):\s*$", line)
    if m:
        cur = m.group(1)
        continue
    m2 = re.match(r"^    (get|post|patch|put|delete|options|head):\s*$", line)
    if m2 and cur:
        print(m2.group(1).upper(), cur)
