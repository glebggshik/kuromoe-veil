#!/usr/bin/env python3
"""Probe media sources for hentai titles."""
import re
import sys

import requests

UA = {"User-Agent": "Mozilla/5.0"}


def jacred(query: str) -> int:
    r = requests.get(
        "https://jac.red/api/v2.0/indexers/all/results",
        params={"Query": query},
        headers=UA,
        timeout=30,
    )
    r.raise_for_status()
    results = r.json().get("Results", [])
    print(f"JacRed '{query}': {len(results)}")
    for t in results[:3]:
        print(" ", (t.get("Title") or "")[:90])
    return len(results)


def sukebei(query: str) -> int:
    r = requests.get(
        "https://sukebei.nyaa.si/",
        params={"f": 0, "c": "0_0", "q": query, "s": "seeders", "o": "desc"},
        headers=UA,
        timeout=30,
    )
    r.raise_for_status()
    titles = re.findall(r'class="torrent-title"[^>]*>.*?title="([^"]+)"', r.text, re.S)
    magnets = re.findall(r'href="(magnet:\?[^"]+)"', r.text)
    print(f"Sukebei '{query}': {len(titles)} titles, {len(magnets)} magnets")
    for t in titles[:3]:
        print(" ", t[:90])
    return len(titles)


def main() -> None:
    queries = sys.argv[1:] or ["Bible Black", "Shin Sekai Yori", "Euphoria"]
    for q in queries:
        print("---")
        jacred(q)
        try:
            sukebei(q)
        except Exception as e:
            print(f"Sukebei error: {e}")


if __name__ == "__main__":
    main()