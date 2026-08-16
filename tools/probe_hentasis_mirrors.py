#!/usr/bin/env python3
import re
import requests

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
MIRRORS = [
    "http://hentasis1.top",
    "http://hentasis2.top",
    "https://hentasis1.top",
    "https://hentasis2.top",
    "http://hentasis.top",
    "https://v1.hentasis.me",
]

for base in MIRRORS:
    try:
        r = requests.get(base + "/", headers=UA, timeout=15, allow_redirects=True)
        ok = r.status_code == 200 and "hentasis" in r.text.lower()[:5000]
        print(base, "->", r.status_code, r.url, "ok" if ok else "fail", len(r.text))
    except Exception as e:
        print(base, "ERR", e)