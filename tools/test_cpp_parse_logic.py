#!/usr/bin/env python3
"""Mirror of fixed C++ parseSearchResults logic."""
import os
import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
    "Referer": "https://animego.org/",
    "Accept-Language": "ru-RU,ru;q=0.9",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
}


def attr_value(attrs: str, name: str) -> str:
    for q in ('"', "'"):
        key = f'{name}={q}'
        pos = attrs.find(key)
        if pos >= 0:
            start = pos + len(key)
            end = attrs.find(q, start)
            if end > start:
                return attrs[start:end]
    return ""


def slug_id_from_href(href: str) -> str:
    href = href.strip().lstrip("/")
    if href.startswith("anime/"):
        href = href[6:]
    if "/" in href:
        href = href.rsplit("/", 1)[-1]
    return href


def parse_slug_id(slug_id: str):
    dash = slug_id.rfind("-")
    if dash <= 0:
        return None
    tail = slug_id[dash + 1 :]
    if not tail.isdigit():
        return None
    return slug_id[:dash], tail


def extract_search_link(chunk: str):
    pos = chunk.find("ani-grid__item-title")
    if pos < 0:
        return None
    a = chunk.find("<a", pos)
    if a < 0 or a > pos + 400:
        return None
    a_end = chunk.find(">", a)
    if a_end < 0:
        return None
    attrs = chunk[a:a_end]
    href = slug_id_from_href(attr_value(attrs, "href"))
    title = attr_value(attrs, "title")
    if not title:
        close = chunk.find("</a>", a_end)
        if close > a_end:
            title = chunk[a_end + 1 : close].strip()
    if not href:
        return None
    return href, title


def parse_search_results(html: str):
    marker = '<div class="ani-grid__item g-col'
    out = []
    seen = set()
    start = 0
    while True:
        mp = html.find(marker, start)
        if mp < 0:
            break
        nxt = html.find(marker, mp + len(marker))
        chunk = html[mp:nxt] if nxt >= 0 else html[mp:]
        start = mp + len(marker)
        link = extract_search_link(chunk)
        if not link:
            continue
        href, title = link
        parsed = parse_slug_id(href)
        if not parsed:
            continue
        slug, aid = parsed
        if aid in seen:
            continue
        seen.add(aid)
        out.append((aid, title, slug))
    return out


for q in ["Sousou no Frieren", "Фрирен", "Shingeki no Kyojin", "Магическая битва"]:
    r = requests.get(
        "https://animego.org/search/anime", params={"q": q}, headers=H, proxies=proxies, timeout=45
    )
    parsed = parse_search_results(r.text)
    print(f"{q!r}: {len(parsed)} results, first={parsed[:2]}")