#!/usr/bin/env python3
"""Check which streaming APIs return hentai by shikimori id."""
import json
import sys

# use anime_client venv parsers
sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client\venv\Lib\site-packages")

from anime_parsers_ru.parser_kodik import KodikParser
from anime_parsers_ru.parser_animego import AnimegoParser

# shikimori ids: Bible Black, Namaiki (Adam's Sweet Agony), recent popular hentai
IDS = {
    "232": "Bible Black",
    "59421": "Namaiki na Imouto / Adam's Sweet Agony",
    "59424": "Iribitari Gal ni Manko Tsukawasete Morau Hanashi",
    "55689": "Mato Seihei no Slave",
}

def try_kodik(sid: str, label: str) -> None:
    p = KodikParser(use_lxml=False)
    try:
        info = p.get_info(id=sid, id_type="shikimori")
        trans = info.get("translations") or info.get("material_data", {}).get("translations") or []
        if isinstance(info, dict) and not trans:
            # older format
            trans = info.get("translation", [])
        print(f"Kodik {sid} ({label}): FOUND, voices={len(trans) if isinstance(trans, list) else '?'}")
        if isinstance(trans, list) and trans:
            print("  sample:", trans[0].get("title") or trans[0])
    except Exception as e:
        print(f"Kodik {sid} ({label}): {type(e).__name__}: {e}")

def try_animego(title: str, label: str) -> None:
    p = AnimegoParser(use_lxml=False)
    try:
        results = p.search(title)
        print(f"Animego '{title}' ({label}): {len(results)} hits")
        if results:
            print("  first:", results[0].get("title"), results[0].get("link"))
    except Exception as e:
        print(f"Animego '{title}' ({label}): {type(e).__name__}: {e}")

if __name__ == "__main__":
    for sid, label in IDS.items():
        try_kodik(sid, label)
    print("---")
    try_animego("Bible Black", "Bible Black")
    try_animego("Мучайся, Адам", "Adam RU")
    try_animego("Namaiki na Imouto", "Adam EN")