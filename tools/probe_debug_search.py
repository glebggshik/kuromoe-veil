#!/usr/bin/env python3
import re
import requests

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

print("=== HENTASIS SEARCH ===")
for q in ["Bible Black", "Bible", "bible black", "Chernaya bibliya", "Намаики"]:
    r = requests.get("http://hentasis1.top/", params={"s": q}, headers=UA, timeout=20)
    links = re.findall(r'href="(http://hentasis1\.top/\d+-[^"]+\.html)"[^>]*>([^<]+)', r.text)
    print(f"Q: {q!r} -> {len(links)} links")
    for u, t in links:
        if "help" not in u and "862" not in u:
            print(f"  {t.strip()[:60]} | {u}")

print("\n=== ANISTAR PAGE (cp1251) ===")
r = requests.get("https://v28.astar.bz/9740-chernaya-bibliya-bible-black.html", headers=UA, timeout=20)
text = r.content.decode("cp1251", errors="replace")
mp4 = re.findall(r"file:\s*['\"]([^'\"]+)['\"]", text, re.I)
torrent = re.findall(r"/engine/gettorrent\.php\?id=(\d+)", text)
print("status", r.status_code, "mp4", len(mp4), "torrents", len(torrent))
if mp4:
    print(" mp4:", mp4[:2])
if torrent:
    print(" torrent ids:", torrent[:5])

print("\n=== HENTASIS direct bible search in HTML ===")
r = requests.get("http://hentasis1.top/", params={"s": "Bible Black"}, headers=UA, timeout=20)
for m in re.finditer(r"bible|библ", r.text, re.I):
    start = max(0, m.start() - 40)
    print(r.text[start:m.start() + 60].replace("\n", " ")[:100])