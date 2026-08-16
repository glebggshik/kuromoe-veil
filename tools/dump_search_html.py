#!/usr/bin/env python3
import os
import re
import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
    "Referer": "https://animego.org/",
    "Accept-Language": "ru-RU,ru;q=0.9",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
}

for q in ["Фрирен", "Sousou no Frieren"]:
    r = requests.get(
        "https://animego.org/search/anime", params={"q": q}, headers=H, proxies=proxies, timeout=45
    )
    idx = r.text.find("ani-grid__item-title")
    print(f"Q={q!r} status={r.status_code} len={len(r.text)} title_idx={idx}")
    if idx >= 0:
        snippet = r.text[idx : idx + 600]
        print(snippet)
        print("---")
        chunk = r.text[idx : idx + 2000]
        for name, pat in [
            ("title_attr", r'ani-grid__item-title[\s\S]*?<a\s+title="([^"]*)"\s+href="/anime/([^"]+)"'),
            ("href_first", r'ani-grid__item-title[\s\S]*?<a\s+href="/anime/([^"]+)"[^>]*title="([^"]*)"'),
            ("text_only", r"ani-grid__item-title[\s\S]*?<a[^>]*>([^<]+)</a>"),
        ]:
            m = re.search(pat, chunk)
            print(name, "->", m.groups() if m else None)