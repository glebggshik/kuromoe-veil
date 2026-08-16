import os
import re
import requests
from base64 import b64decode

proxy = os.environ.get("KODIK_PROXY", "")
proxies = {"http": proxy, "https": proxy}
headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}

script_path = "/assets/js/app.serial.1376c0640f7e13b292b398a55d0ddc200e4f22bc8c177532b0a7ab344434be72.js"
url = "https://kodikplayer.com" + script_path
print("fetch", url)
r = requests.get(url, proxies=proxies, headers=headers, timeout=90)
print("status", r.status_code, "len", len(r.text))
script = r.text
ap = script.find("$.ajax")
print("$.ajax", ap)
for marker in ["cache:!1", "cache:!0", "cache:false", "cache:0"]:
    print(marker, script.find(marker, max(0, ap)))

if ap >= 0:
    print("ctx", repr(script[ap : ap + 150]))

def try_decode(chunk):
    try:
        return b64decode(chunk.encode("ascii")).decode("utf-8")
    except Exception:
        return None

if ap >= 0:
    for cp_marker in ["cache:!1", "cache:!0"]:
        cp = script.find(cp_marker, ap)
        if cp < ap:
            continue
        for end_adj in range(0, 10):
            for off in range(20, 45):
                dec = try_decode(script[ap + off : cp - end_adj])
                if dec and dec.startswith("/") and len(dec) >= 8:
                    print("hit", cp_marker, "off", off, "end_adj", end_adj, repr(dec))

    window = script[ap : ap + 800]
    for m in re.finditer(r'"([A-Za-z0-9+/=]{10,})"', window):
        dec = try_decode(m.group(1))
        if dec and dec.startswith("/"):
            print("quoted", repr(dec))
    for m in re.finditer(r"'([A-Za-z0-9+/=]{10,})'", window):
        dec = try_decode(m.group(1))
        if dec and dec.startswith("/"):
            print("quoted_s", repr(dec))