#!/usr/bin/env python3
"""Simulate Hentasis/AniStar search flow like the C++ client."""
import re
import requests

UA = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "text/html,application/xhtml+xml",
    "Accept-Language": "ru-RU,ru;q=0.9,en;q=0.8",
}

QUERIES = ["Bible Black", "Мучайся, Адам", "Namaiki na Imouto", "Iribitari Gal"]

HENTASIS_MIRRORS = [
    "http://hentasis1.top",
    "http://hentasis2.top",
    "https://v3.hentasis5.top",
    "https://v1.hentasis.me",
]

ANISTAR_MIRRORS = [
    "https://anistar.org",
    "https://v28.astar.bz",
    "https://v20.astar.bz",
]

LINK_RE = re.compile(r'<a[^>]+href="([^"]*/\d+-[^"]+\.html)"[^>]*>([^<]+)</a>', re.I)
LINK_RE2 = re.compile(r'<a[^>]+href="([^"]*/hentai/\d+-[^"]+\.html)"[^>]*>([^<]+)</a>', re.I)
MP4_RE = re.compile(r"file:\s*['\"]([^'\"]+)['\"]", re.I)
ACT_RE = re.compile(r'<a[^>]+class=["\']act["\'][^>]*>([^<]+)</a>', re.I)


def probe_search(base: str, query: str, paths: list[str]) -> None:
    for path_tpl in paths:
        path = path_tpl.format(q=requests.utils.quote(query))
        try:
            r = requests.get(base + path, headers=UA, timeout=20, allow_redirects=True)
            enc = r.encoding or "utf-8"
            text = r.content.decode(enc, errors="replace")
            links = LINK_RE.findall(text) + LINK_RE2.findall(text)
            mp4 = MP4_RE.findall(text)
            print(f"  {base}{path[:60]} -> {r.status_code} final={r.url[:50]} enc={enc} links={len(links)} mp4={len(mp4)}")
            if links:
                print(f"    sample: {links[0][1][:60]} | {links[0][0][:60]}")
        except Exception as e:
            print(f"  {base} ERR {e}")


def probe_mirror(base: str) -> None:
    try:
        r = requests.get(base + "/", headers=UA, timeout=15, allow_redirects=True)
        text = r.content.decode(r.encoding or "utf-8", errors="replace")
        act = ACT_RE.findall(text)
        base_tag = re.search(r'<base href="([^"]+)"', text, re.I)
        print(f"{base} -> {r.status_code} {r.url[:55]} len={len(text)} act={act[:2]} base={base_tag.group(1) if base_tag else None}")
    except Exception as e:
        print(f"{base} ERR {e}")


print("=== MIRROR DISCOVERY ===")
for m in ["https://anistar.org/", "https://v28.astar.bz/", "http://hentasis2.top/"]:
    probe_mirror(m.rstrip("/"))

print("\n=== HENTASIS SEARCH ===")
for q in QUERIES[:2]:
    print(f"Query: {q}")
    for base in HENTASIS_MIRRORS[:2]:
        probe_search(base, q, ["/?s={q}"])

print("\n=== ANISTAR SEARCH ===")
for q in QUERIES[:2]:
    print(f"Query: {q}")
    for base in ANISTAR_MIRRORS[:2]:
        probe_search(
            base,
            q,
            [
                "/index.php?do=search&subaction=search&story={q}",
                "/?s={q}",
            ],
        )

print("\n=== SAMPLE PAGE MP4 ===")
for url in [
    "http://hentasis1.top/732-oujo-onna-kishi.html",
    "https://v28.astar.bz/hentai/10897-bible-black.html",
]:
    try:
        r = requests.get(url, headers=UA, timeout=20)
        text = r.content.decode(r.encoding or "utf-8", errors="replace")
        mp4 = MP4_RE.findall(text)
        print(url, "status", r.status_code, "mp4", len(mp4), mp4[:2])
    except Exception as e:
        print(url, e)