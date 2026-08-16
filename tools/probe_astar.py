#!/usr/bin/env python3
import re
import requests

UA = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "text/html,application/xhtml+xml",
    "Accept-Language": "ru-RU,ru;q=0.9,en;q=0.8",
}

MIRRORS = [
    "https://anistar.org",
    "https://www154.anistar.org",
    "https://v20.astar.bz",
    "https://v8.astar.bz",
    "https://v6.astar.bz",
    "https://anistar.my",
]

for base in MIRRORS:
    try:
        r = requests.get(base + "/hentai/", headers=UA, timeout=20, allow_redirects=True)
        ok = r.status_code == 200 and len(r.text) > 10000
        print(base, "->", r.status_code, r.url[:60], "len", len(r.text), "ok" if ok else "fail")
    except Exception as e:
        print(base, "ERR", str(e)[:80])

print("--- search Bible Black ---")
for base in ["https://v20.astar.bz", "https://v8.astar.bz", "https://anistar.org"]:
    try:
        r = requests.get(base + "/search/", params={"q": "Bible Black"}, headers=UA, timeout=20)
        print(base, "search", r.status_code, len(r.text))
        links = re.findall(r'href="(/hentai/[^"]+\.html)"', r.text)
        if not links:
            links = re.findall(r'href="(https?://[^"]+/hentai/[^"]+)"', r.text)
        print("  hentai links", links[:5])
        # sample page
        if links:
            page_url = links[0] if links[0].startswith("http") else base + links[0]
            p = requests.get(page_url, headers=UA, timeout=20)
            for pat in [r"file:\s*['\"]([^'\"]+)['\"]", r'<iframe[^>]+src="([^"]+)"', r'"hls":"([^"]+)"', r'source src="([^"]+)"']:
                m = re.findall(pat, p.text, re.I)
                if m:
                    print("  video", pat[:30], m[:3])
    except Exception as e:
        print(base, e)

print("--- sample hentai page ---")
base = "https://v20.astar.bz"
r = requests.get(base + "/hentai/", headers=UA, timeout=20)
links = re.findall(r'href="(/hentai/[^"]+)"', r.text)
print("list links", len(links), links[:8])
links2 = re.findall(r'href="(https://v20\.astar\.bz/[^"]+)"', r.text)
print("abs links sample", links2[:8])
if links:
    p = requests.get(base + links[0], headers=UA, timeout=20)
    print("page len", len(p.text))
    for pat in [
        r"file:\s*['\"]([^'\"]+)['\"]",
        r'<iframe[^>]+src="([^"]+)"',
        r'data-src="([^"]+)"',
        r'"(https?://[^"]+\.m3u8[^"]*)"',
        r'"(https?://[^"]+\.mp4[^"]*)"',
    ]:
        m = re.findall(pat, p.text, re.I)
        if m:
            print(" ", pat[:40], m[:4])
    # search form
    forms = re.findall(r'<form[^>]+action="([^"]+)"', p.text)
    print("forms on page", forms[:3])
# try site search variants
for url in [
    base + "/?s=Bible+Black",
    base + "/index.php?do=search&subaction=search&story=Bible+Black",
]:
    rr = requests.get(url, headers=UA, timeout=20)
    hl = re.findall(r'href="(/hentai/\d+-[^"]+\.html)"', rr.text)
    print(url.split(base)[1][:50], rr.status_code, "hits", len(hl), hl[:2])