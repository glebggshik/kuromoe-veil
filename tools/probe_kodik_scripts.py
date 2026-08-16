import re
import json
import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config
from anime_parsers_ru import KodikParser

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()

r = requests.post(
    "https://kodik-api.com/search",
    data={
        "token": token,
        "shikimori_id": "16498",
        "translation_id": "609",
        "with_material_data": "false",
        "limit": "1",
    },
    proxies=proxies,
    timeout=30,
)
link = "https:" + r.json()["results"][0]["link"]
print("api link", link)

html = requests.get(link, proxies=proxies, timeout=30).text
print("geo blocked", "запрещено" in html)

scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
for i, m in enumerate(scripts):
    src_m = re.search(r'src="([^"]+)"', m.group(1))
    src = src_m.group(1) if src_m else "(inline)"
    print(f"[{i}] src={src} inline_len={len(m.group(2))}")

# episode 1 page via parser
p = KodikParser(token=token, proxy=config.get_kodik_proxy(), validate_token=False)
url = p.get_m3u8_playlist_link("16498", "shikimori", 1, "609")
print("m3u8 ok", url[:100])

# compare C++ script index logic on episode page
# replicate get_link episode fetch
from bs4 import BeautifulSoup

embed = p.get_embed_link("16498", "shikimori")
data = requests.get(embed, proxies=proxies, timeout=30).text
soup = BeautifulSoup(data, "html.parser")
container = soup.find("div", {"class": "serial-translations-box"}).find("select")
media_hash = media_id = None
for translation in container.find_all("option"):
    if translation.get("data-id") == "609":
        media_hash = translation["data-media-hash"]
        media_id = translation["data-media-id"]
        break
ep_url = f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p?min_age=16&first_url=false&season=1&episode=1"
page = requests.get(ep_url, proxies=proxies, timeout=30).text
scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page))
print("\nepisode page scripts:")
for i, m in enumerate(scripts):
    src_m = re.search(r'src="([^"]+)"', m.group(1))
    src = src_m.group(1) if src_m else "(inline)"
    inline = m.group(2)
    has_type = ".type = '" in inline
    print(f"[{i}] src={src} has_type={has_type}")

# C++ logic: 2nd script WITH src
src_idx = 0
for i, m in enumerate(scripts):
    src_m = re.search(r'src="([^"]+)"', m.group(1))
    if src_m:
        if src_idx == 1:
            print("C++ scriptSrcAtTagIndex(1) =", src_m.group(1))
        src_idx += 1

print("Python scripts[1].src =", scripts[1].group(1))