import os
import re
import requests
from base64 import b64decode

proxy = os.environ.get("KODIK_PROXY", "")
proxies = {"http": proxy, "https": proxy}
token = "56a768d08f43091901c44b54fe970049"

r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=proxies,
    timeout=60,
)
link = "https:" + r.json()["results"][0]["link"]
print("player", link)
html = requests.get(link, proxies=proxies, timeout=60).text
scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
script_url = re.search(r'src="([^"]+)"', scripts[1].group(1)).group(1)
print("script_url", script_url)
script = requests.get("https://kodikplayer.com" + script_url, proxies=proxies, timeout=60).text
print("script len", len(script))
ap = script.find("$.ajax")
print("$.ajax at", ap)
for marker in ["cache:!1", "cache:!0", "cache:false", "cache: false"]:
    print(marker, script.find(marker, ap))

# show context around $.ajax
if ap >= 0:
    print("context:", repr(script[ap : ap + 120]))

# try decode with various offsets
cp = script.find("cache:!1", ap)
if cp < 0:
    cp = script.find("cache:!0", ap)
for off in range(20, 45):
    if ap < 0 or cp < ap:
        continue
    chunk = script[ap + off : cp - 3]
    try:
        dec = b64decode(chunk.encode()).decode()
        if dec.startswith("/"):
            print("OK off", off, repr(dec))
    except Exception:
        pass

# find all base64-like strings near ajax
for m in re.finditer(r'"([A-Za-z0-9+/=]{12,})"', script[ap : ap + 500] if ap >= 0 else ""):
    try:
        dec = b64decode(m.group(1).encode()).decode()
        if dec.startswith("/"):
            print("quoted b64", repr(dec))
    except Exception:
        pass