#!/usr/bin/env python3
import os
import re
import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
Q = "Провожающая в последний путь Фрирен"

block_re = re.compile(
    r'<div class="ani-grid__item g-col([\s\S]*?)(?=<div class="ani-grid__item g-col|$)'
)

for xhr in (False, True):
    H = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
        "Referer": "https://animego.org/",
        "Accept-Language": "ru-RU,ru;q=0.9",
    }
    if xhr:
        H["X-Requested-With"] = "XMLHttpRequest"
        H["Accept"] = "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8"
    r = requests.get(
        "https://animego.org/search/anime", params={"q": Q}, headers=H, proxies=proxies, timeout=45
    )
    blocks = block_re.findall(r.text)
    print(
        f"xhr={xhr} status={r.status_code} len={len(r.text)} "
        f"blocks={len(blocks)} grid={'ani-grid__item g-col' in r.text} "
        f"json={r.text.lstrip()[:1] == '{'}"
    )