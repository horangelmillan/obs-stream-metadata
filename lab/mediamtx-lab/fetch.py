import urllib.request, os
base = "https://github.com/bluenviron/mediamtx/releases/download/v1.21.3"
dl = "lab/mediamtx-lab/downloads"
os.makedirs(dl, exist_ok=True)
for f in ["checksums.sha256", "mediamtx_v1.21.3_windows_amd64.zip"]:
    url = base + "/" + f
    dst = os.path.join(dl, f)
    print("downloading", url, flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "t078-lab"})
    with urllib.request.urlopen(req, timeout=120) as r, open(dst, "wb") as fh:
        total = 0
        while True:
            chunk = r.read(1024 * 256)
            if not chunk:
                break
            fh.write(chunk)
            total += len(chunk)
        print("saved", dst, total, "bytes", flush=True)
print("done")
