#!/usr/bin/env python3
"""Quick AnimeGO CVH diagnostic."""
import os
import re
import sys

import requests
from bs4 import BeautifulSoup

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    ),
    "Accept-Language": "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7",
    "Referer": "https://animego.org/",
    "X-Requested-With": "XMLHttpRequest",
}


def search(query: str):
    url = "https://animego.org/search/anime"
    r = requests.get(url, params={"q": query}, headers=HEADERS, proxies=proxies, timeout=45)
    print(f"\n=== search '{query}' -> {r.status_code} ({len(r.text)} bytes) ===")
    if r.status_code != 200:
        print(r.text[:300])
        return []
    out = []
    for item in BeautifulSoup(r.text, "html.parser").select("div.ani-grid__item"):
        link = (
            item.select_one("a.ani-grid__item-body")
            or item.select_one("div.ani-grid__item-title a")
            or item.select_one('a[href*="/anime/"]')
        )
        if not link:
            continue
        href = link.get("href", "").strip("/")
        m = re.match(r"anime/(.+)-(\d+)$", href)
        if not m:
            continue
        title = link.get_text(strip=True)
        out.append({"id": m.group(2), "slug": m.group(1), "title": title})
    for row in out[:5]:
        print(f"  {row['id']} | {row['title']}")
    return out


def player_html(body: str) -> str:
    import html as html_mod
    import json

    body = body.strip()
    if not body.startswith("{"):
        return body
    data = json.loads(body)
    return html_mod.unescape(data.get("data", {}).get("content", ""))


def voices(animego_id: str):
    url = f"https://animego.org/player/{animego_id}"
    r = requests.get(url, headers=HEADERS, proxies=proxies, timeout=45)
    print(f"\n=== player {animego_id} -> {r.status_code} ({len(r.text)} bytes) ===")
    if r.status_code != 200:
        print(r.text[:300])
        return
    content = player_html(r.text)
    print(f"parsed html len: {len(content)}")
    soup = BeautifulSoup(content, "html.parser")
    prov = soup.find("div", {"id": "provider"})
    if not prov:
        print("NO provider div")
        print("snippet:", r.text[:800])
        return
    btns = prov.find_all("button")
    print(f"buttons total: {len(btns)}")
    cvh = 0
    for b in btns:
        p = b.get("data-provider-title")
        if p == "CVH":
            cvh += 1
        print(
            f"  provider={p!r} title={b.get('data-translation-title')!r} "
            f"ptranslation={b.get('data-ptranslation')!r} "
            f"player={(b.get('data-player') or '')[:70]!r}"
        )
    print(f"CVH count: {cvh}")


def try_ids(ids):
    for animego_id in ids:
        voices(animego_id)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--ids":
        try_ids(sys.argv[2:])
        return
    queries = sys.argv[1:] or ["Тетрадь смерти", "Death Note", "Евангелион", "Neon Genesis Evangelion"]
    for q in queries:
        results = search(q)
        if results:
            voices(results[0]["id"])


if __name__ == "__main__":
    main()