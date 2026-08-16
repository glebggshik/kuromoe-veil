#!/usr/bin/env python3
import re
import requests

UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

def probe_hentasis():
    # search + sample page
    for q in ["Bible Black", "Namaiki", "Adam"]:
        r = requests.get("http://hentasis1.top/", params={"s": q}, headers=UA, timeout=25)
        links = re.findall(r'href="(https?://hentasis1\.top/\d+-[^"]+\.html)"', r.text)
        print(f"Hentasis search '{q}': {len(links)} links")
        if links:
            print(" ", links[0])
    page = requests.get("http://hentasis1.top/732-oujo-onna-kishi.html", headers=UA, timeout=25)
    print("Hentasis page len", len(page.text))
    for pat in [
        r'<iframe[^>]+src="([^"]+)"',
        r'file:\s*["\']([^"\']+)["\']',
        r'source\s+src="([^"]+)"',
        r'data-url="([^"]+)"',
        r'video[^>]+src="([^"]+)"',
        r'player[^>]+src="([^"]+)"',
        r'/uploads/[^"\']+\.mp4',
        r'kodik[^"\']+',
        r'moonwalk[^"\']+',
    ]:
        m = re.findall(pat, page.text, re.I)
        if m:
            print("  pattern", pat[:40], "->", m[:3])

def probe_allhen():
    r = requests.get("https://20.allhen.online/", params={"q": "Bible Black"}, headers=UA, timeout=25)
    print("Allhen search len", len(r.text))
    links = re.findall(r'href="(/[^"]+)"[^>]*class="[^"]*title', r.text)
    if not links:
        links = re.findall(r'href="(/anime/[^"]+)"', r.text)
    if not links:
        links = re.findall(r'href="(/hentai/[^"]+)"', r.text)
    print("Allhen links sample", links[:5])
    # try internal search API
    for url in [
        "https://20.allhen.online/search?query=Bible+Black",
        "https://20.allhen.online/list/anime?search=Bible+Black",
    ]:
        try:
            rr = requests.get(url, headers=UA, timeout=20)
            print(url, rr.status_code, len(rr.text))
            anime = re.findall(r'href="(/[^\"]{5,80})"', rr.text)
            print("  hrefs", anime[:8])
        except Exception as e:
            print(url, e)

def probe_allhen_anime():
    r = requests.get("https://20.allhen.online/", headers=UA, timeout=25)
    links = set(re.findall(r'href="(/[^"]+)"', r.text))
    print("Allhen nav (anime-related):")
    for k in sorted(links):
        if any(x in k.lower() for x in ("anime", "hentai", "video", "watch", "ozvuch", "serial", "smotret")):
            print(" ", k)
    # try site search form action
    forms = re.findall(r"<form[^>]+action=\"([^\"]+)\"[^>]*>", r.text, re.I)
    print("forms", forms[:5])

def probe_hentasis_bible():
    r = requests.get("http://hentasis1.top/", params={"s": "Bible Black"}, headers=UA, timeout=25)
    for m in re.finditer(r'href="(http://hentasis1\.top/\d+-[^"]+)"[^>]*>([^<]+)', r.text):
        title = re.sub(r"\s+", " ", m.group(2)).strip()
        if "bible" in title.lower() or "библ" in title.lower():
            print("MATCH", title[:80], m.group(1))
    page = None
    for u in re.findall(r"http://hentasis1\.top/\d+-[^\"]+\.html", r.text):
        if "bible" in u.lower():
            page = u
            break
    if not page:
        # fallback known id search in page text
        m = re.search(r"hentasis1\.top/(\d+-[^\"]*bible[^\"]*\.html)", r.text, re.I)
        if m:
            page = "http://" + m.group(0)
    if page:
        p = requests.get(page, headers=UA, timeout=25)
        mp4 = re.findall(r"file:\s*['\"]([^'\"]+)['\"]", p.text)
        print("Bible page mp4:", len(mp4))
        for u in mp4[:4]:
            print(" ", u[:100])

if __name__ == "__main__":
    probe_hentasis()
    print("---")
    probe_allhen()
    print("---")
    probe_allhen_anime()
    print("---")
    probe_hentasis_bible()