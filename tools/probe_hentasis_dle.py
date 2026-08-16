#!/usr/bin/env python3
import re
import requests

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
base = "http://hentasis1.top"

paths = [
    "/?s=Bible+Black",
    "/index.php?do=search&subaction=search&story=Bible+Black",
    "/search/?q=Bible+Black",
]

for path in paths:
    r = requests.get(base + path, headers=UA, timeout=20)
    links = re.findall(r'href="(http://hentasis1\.top/\d+-[^"]+\.html)"[^>]*>([^<]+)', r.text)
    bible = [x for x in links if "bible" in x[1].lower() or "библ" in x[1].lower()]
    print(path, "status", r.status_code, "total", len(links), "bible hits", len(bible))
    for u, t in bible[:3]:
        print(" ", t[:70], u)