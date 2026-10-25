import hashlib, zipfile, os
p = "lab/mediamtx-lab/downloads/mediamtx_v1.21.3_windows_amd64.zip"
h = hashlib.sha256()
with open(p, "rb") as f:
    for chunk in iter(lambda: f.read(1024 * 512), b""):
        h.update(chunk)
print("sha256:", h.hexdigest())
print("expected: b2f5246b07cb72961df03b8d5e8cad43dc657bcf3f49a87bb031bdf22721af74")
print("match:", h.hexdigest() == "b2f5246b07cb72961df03b8d5e8cad43dc657bcf3f49a87bb031bdf22721af74")
dst = "lab/mediamtx-lab/bin"
os.makedirs(dst, exist_ok=True)
with zipfile.ZipFile(p) as z:
    print("members:", z.namelist())
    z.extractall(dst)
print("extracted to", dst)
