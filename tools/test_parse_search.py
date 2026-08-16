#!/usr/bin/env python3
import os
import re
import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
    "Referer": "https://animego.org/",
}


def parse_cpp_style(html: str):
    out = []
    seen = set()
    block_re = re.compile(
        r'<div class="ani-grid__item g-col([\s\S]*?)(?=<div class="ani-grid__item g-col|$)'
    )
    title_attr_re = re.compile(
        r'ani-grid__item-title[\s\S]*?<a\s+title="([^"]*)"\s+href="/anime/([^"]+)"'
    )
    orig_re = re.compile(
        r'ani-grid__item-body[\s\S]*?fw-lighter[^>]*>\s*([^<]+?)\s*</div>'
    )
    id_re = re.compile(r"^(.+)-(\d+)$")
    for chunk in block_re.findall(html):
        chunk = '<div class="ani-grid__item g-col' + chunk
        tm = title_attr_re.search(chunk)
        if not tm:
            continue
        title, href = tm.group(1).strip(), tm.group(2).strip("/")
        path = href.split("/")[-1] if "/" in href else href
        if path.startswith("anime/"):
            path = path[6:]
        im = id_re.match(path)
        if not im:
            continue
        aid = im.group(2)
        if aid in seen:
            continue
        seen.add(aid)
        orig = ""
        om = orig_re.search(chunk)
        if om:
            orig = om.group(1).strip()
        out.append((aid, title, orig))
    return out


def score_field(field, wanted):
    def norm(s):
        return re.sub(r"\s+", " ", s.strip().lower())

    fn = norm(field)
    best = 0
    for w in wanted:
        wn = norm(w)
        if not wn:
            continue
        if fn == wn:
            best = max(best, 100)
        elif wn in fn or fn in wn:
            best = max(best, 75)
    return best


for q in ["Тетрадь смерти", "Магическая битва", "Кайфуем", "Дандадан", "Death Note"]:
    r = requests.get(
        "https://animego.org/search/anime", params={"q": q}, headers=H, proxies=proxies, timeout=45
    )
    parsed = parse_cpp_style(r.text)
    print(f"Q={q!r} status={r.status_code} parsed={len(parsed)} first={parsed[:2] if parsed else []}")

for sid in [31964, 32281, 16498, 60593]:
    r = requests.get(f"https://shikimori.one/api/animes/{sid}", headers=H, timeout=30)
    if r.status_code == 200:
        d = r.json()
        ru = d.get("russian") or ""
        en = d.get("name") or ""
        print(f"shiki {sid}: ru={ru!r} en={en!r}")
        for q in [ru, en]:
            if not q:
                continue
            sr = requests.get(
                "https://animego.org/search/anime",
                params={"q": q},
                headers=H,
                proxies=proxies,
                timeout=45,
            )
            parsed = parse_cpp_style(sr.text)
            scores = [(score_field(t, [ru, en]), aid, t) for aid, t, _ in parsed[:10]]
            scores.sort(reverse=True)
            print(f"  search {q!r} -> {len(parsed)} results, top scores: {scores[:3]}")