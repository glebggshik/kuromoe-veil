#!/usr/bin/env python3
"""Full CVH flow test for titles user tried."""
import os
import html
import json
import re
import requests
from bs4 import BeautifulSoup

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
    "X-Requested-With": "XMLHttpRequest",
    "Referer": "https://animego.org/",
}

CASES = [
    ("Атака титанов", "Shingeki no Kyojin"),
    ("Тетрадь смерти", "Death Note"),
    ("Стальной алхимик: Братство", "Fullmetal Alchemist: Brotherhood"),
    ("Ванпанчмен", "One Punch Man"),
    ("Моя геройская академия", "Boku no Hero Academia"),
]


def parse_search(html_text):
    block_re = re.compile(
        r'<div class="ani-grid__item g-col([\s\S]*?)(?=<div class="ani-grid__item g-col|$)'
    )
    title_re = re.compile(
        r'ani-grid__item-title[\s\S]*?<a\s+title="([^"]*)"\s+href="/anime/([^"]+)"'
    )
    id_re = re.compile(r"^(.+)-(\d+)$")
    out = []
    for chunk in block_re.findall(html_text):
        chunk = '<div class="ani-grid__item g-col' + chunk
        m = title_re.search(chunk)
        if not m:
            continue
        title, href = m.group(1), m.group(2).strip("/")
        path = href.split("/")[-1]
        if path.startswith("anime/"):
            path = path[6:]
        im = id_re.match(path)
        if im:
            out.append((im.group(2), title))
    return out


def player_html(body):
    body = body.strip()
    if body.startswith("{"):
        data = json.loads(body)
        return html.unescape(data.get("data", {}).get("content", ""))
    return body


def cvh_voices(content):
    soup = BeautifulSoup(content, "html.parser")
    prov = soup.find("div", {"id": "provider"})
    if not prov:
        return 0, "NO provider div"
    cvh = 0
    labels = []
    for b in prov.find_all("button"):
        p = b.get("data-provider-title")
        if p == "CVH":
            cvh += 1
            labels.append(b.get("data-translation-title"))
    return cvh, labels[:5]


for ru, en in CASES:
    print(f"\n=== {ru} / {en} ===")
    for q in [en, ru]:
        r = requests.get(
            "https://animego.org/search/anime",
            params={"q": q},
            headers=H,
            proxies=proxies,
            timeout=45,
        )
        results = parse_search(r.text)
        print(f"  search {q!r}: {len(results)} hits")
        for aid, title in results[:5]:
            if ru.lower()[:8] in title.lower() or en.split()[0].lower() in title.lower():
                pr = requests.get(f"https://animego.org/player/{aid}", headers=H, proxies=proxies, timeout=45)
                content = player_html(pr.text)
                cvh, info = cvh_voices(content)
                print(f"    -> {aid} {title!r}: CVH={cvh} {info}")
                break
        else:
            if results:
                aid, title = results[0]
                pr = requests.get(f"https://animego.org/player/{aid}", headers=H, proxies=proxies, timeout=45)
                content = player_html(pr.text)
                cvh, info = cvh_voices(content)
                print(f"    -> first {aid} {title!r}: CVH={cvh} {info}")