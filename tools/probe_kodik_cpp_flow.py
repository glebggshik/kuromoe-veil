"""Reproduce C++ KodikClient stream resolution with proxy."""
import json
import re
import sys
import requests
from base64 import b64decode

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

SHIKIMORI_ID = "16498"
EPISODE = 1
TRANSLATION_ID = "609"
PROXIES = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}


def attr_value(attrs, name):
    m = re.search(name + r'="([^"]*)"', attrs)
    if m:
        return m.group(1)
    m = re.search(name + r"='([^']*)'", attrs)
    return m.group(1) if m else ""


def script_src_at_tag_index(html, tag_index):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    idx = 0
    for m in scripts:
        src = attr_value(m.group(1), "src")
        if src:
            if idx == tag_index:
                return src
            idx += 1
    m = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', html)
    return m.group(1) if m else ""


def inline_script_at_tag_index(html, tag_index):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    for i, m in enumerate(scripts):
        if i == tag_index:
            return m.group(2)
    return ""


def extract_json_after(text, marker):
    start = text.find(marker)
    if start < 0:
        return None
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return json.loads(text[brace : i + 1])
    return None


def extract_quoted_after(text, marker):
    idx = text.find(marker)
    if idx < 0:
        return ""
    start = idx + len(marker)
    end = text.find("'", start)
    return text[start:end] if end >= 0 else ""


token = config.get_kodik_token()
print("token ok")

# API search for translation-specific link (proposed C++ improvement)
r = requests.post(
    "https://kodik-api.com/search",
    data={
        "token": token,
        "shikimori_id": SHIKIMORI_ID,
        "translation_id": TRANSLATION_ID,
        "with_material_data": "false",
        "limit": "1",
    },
    proxies=PROXIES,
    timeout=30,
)
api_link = "https:" + r.json()["results"][0]["link"]
print("api translation link", api_link)

# Current C++ path: generic embed
embed_url = (
    f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
    f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D{SHIKIMORI_ID}"
    f"&token={token}&shikimoriID={SHIKIMORI_ID}"
)
embed = requests.get(embed_url, proxies=PROXIES, timeout=30).json()
generic_link = embed["link"]
if generic_link.startswith("//"):
    generic_link = "https:" + generic_link
print("generic embed link", generic_link)

for label, link in [("api", api_link), ("generic", generic_link)]:
    print(f"\n=== {label} ===")
    html = requests.get(link, proxies=PROXIES, timeout=30).text
    if "Видео запрещено" in html:
        print("GEO BLOCKED")
        continue
    url_params = extract_json_after(html, "urlParams")
    serial = "kodikplayer.com/s" in link[link.find("kodikplayer.com/") :]
    box = "serial-translations-box" if serial else "movie-translations-box"
    box_pos = html.find(box)
    select_pos = html.find("<select", box_pos)
    select_end = html.index("</select>", select_pos)
    select_block = html[select_pos:select_end]
    media_id = media_hash = ""
    for m in re.finditer(r"<option\s+([^>]+)>", select_block):
        attrs = m.group(1)
        if attr_value(attrs, "data-id") == TRANSLATION_ID:
            media_id = attr_value(attrs, "data-media-id")
            media_hash = attr_value(attrs, "data-media-hash")
            break
    print("media", media_id, media_hash[:16] if media_hash else "")
    if not media_id:
        print("translation not found by data-id")
        continue
    ep_url = f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p?min_age=16&first_url=false&season=1&episode={EPISODE}"
    page = requests.get(ep_url, proxies=PROXIES, timeout=30).text
    print("episode status len", len(page))
    script_url = script_src_at_tag_index(page, 1)
    hash_container = inline_script_at_tag_index(page, 4)
    video_type = extract_quoted_after(hash_container, ".type = '")
    video_hash = extract_quoted_after(hash_container, ".hash = '")
    video_id = extract_quoted_after(hash_container, ".id = '")
    print("video meta", bool(script_url), video_type, video_id, video_hash[:12] if video_hash else "")
    if not all([script_url, video_type, video_hash, video_id]):
        continue
    script = requests.get("https://kodikplayer.com" + script_url, proxies=PROXIES, timeout=30).text
    ajax_pos = script.find("$.ajax")
    cache_pos = script.find("cache:!1")
    post_link = b64decode(script[ajax_pos + 30 : cache_pos - 33]).decode()
    form = {
        "hash": video_hash,
        "id": video_id,
        "type": video_type,
        "d": url_params["d"],
        "d_sign": url_params["d_sign"],
        "pd": url_params["pd"],
        "pd_sign": url_params["pd_sign"],
        "ref": "",
        "ref_sign": url_params["ref_sign"],
        "bad_user": "true",
        "cdn_is_working": "true",
    }
    resp = requests.post("https://kodikplayer.com" + post_link, data=form, proxies=PROXIES, timeout=30).json()
    if "error" in resp:
        print("error", resp["error"])
        continue
    links = resp["links"]
    max_q = max(int(k) for k in links)
    src = links[str(max_q)][0]["src"]
    print("OK stream", src[:90])