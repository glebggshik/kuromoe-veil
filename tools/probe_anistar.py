#!/usr/bin/env python3
import re
import requests

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

MIRRORS = [
    "https://anistar.org",
    "https://www.anistar.org",
    "https://online.anistar.org",
    "https://anistar.ru",
]

def probe_base(url: str) -> None:
    try:
        r = requests.get(url + "/", headers=UA, timeout=20, allow_redirects=True)
        print(url, "->", r.status_code, r.url[:70], "len", len(r.text))
        if r.status_code == 200:
            t = re.search(r"<title>([^<]+)", r.text, re.I)
            if t:
                print("  title:", t.group(1)[:80])
    except Exception as e:
        print(url, "ERR", e)

def probe_search(base: str, q: str) -> None:
    for path in ["/search", "/?s=" + q, "/index.php?do=search&subaction=search&story=" + q]:
        try:
            if path.startswith("/?"):
                r = requests.get(base + path, headers=UA, timeout=20)
            elif "story=" in path:
                r = requests.get(base + path, headers=UA, timeout=20)
            else:
                r = requests.get(base, params={"s": q}, headers=UA, timeout=20)
            links = re.findall(r'href="(' + re.escape(base) + r'/[^"]+)"', r.text)
            mp4 = re.findall(r"file:\s*['\"]([^'\"]+)['\"]", r.text)
            print(f"  {path[:50]} status={r.status_code} links={len(links)} mp4={len(mp4)}")
        except Exception as e:
            print("  ", path, e)

if __name__ == "__main__":
    for m in MIRRORS:
        probe_base(m)
    print("---")
    for m in MIRRORS:
        if "anistar.org" in m:
            print("search on", m)
            probe_search(m, "Bible Black")
            break