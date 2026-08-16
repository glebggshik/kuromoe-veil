#!/usr/bin/env python3
import os
import html
import json
import re
from collections import Counter

import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {"User-Agent": "Mozilla/5.0", "Referer": "https://animego.org/"}
XHR = {**H, "X-Requested-With": "XMLHttpRequest"}

title_re = re.compile(
    r'ani-grid__item-title[\s\S]*?<a\s+title="([^"]*)"\s+href="/anime/([^"]+)"'
)

for q in ["Атака титанов", "Shingeki no Kyojin", "attack on titan", "титаны"]:
    r = requests.get(
        "https://animego.org/search/anime", params={"q": q}, headers=H, proxies=proxies, timeout=45
    )
    print(f"\nQ={q!r} ({len(r.text)} bytes)")
    for m in title_re.finditer(r.text):
        href = m.group(2)
        aid = href.rsplit("-", 1)[-1]
        if "titan" in href or "titan" in m.group(1).lower() or "титан" in m.group(1).lower():
            pr = requests.get(f"https://animego.org/player/{aid}", headers=XHR, proxies=proxies, timeout=45)
            body = pr.text
            if body.strip().startswith("{"):
                body = html.unescape(json.loads(body)["data"]["content"])
            cvh = len(re.findall(r'data-provider-title="CVH"', body))
            print(f"  {aid} {m.group(1)!r} CVH={cvh}")