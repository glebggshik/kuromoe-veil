"""Test improved Kodik flow: API link + episode from series box."""
import json
import re
import sys
import requests
from base64 import b64decode

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()
SHIKIMORI_ID = "16498"
EPISODE = 1
TRANSLATION_ID = "609"


def attr(attrs, name):
    m = re.search(name + r'="([^"]*)"', attrs)
    return m.group(1) if m else ""


def extract_json_after(text, marker):
    start = text.find(marker)
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
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


def fetch_stream_from_page(html, url_params):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    script_url = scripts[1].group(1)
    script_url = attr(script_url, "src")
    hash_container = scripts[4].group(2)
    video_type = extract_quoted_after(hash_container, ".type = '")
    video_hash = extract_quoted_after(hash_container, ".hash = '")
    video_id = extract_quoted_after(hash_container, ".id = '")
    script = requests.get("https://kodikplayer.com" + script_url, proxies=proxies, timeout=30).text
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
    resp = requests.post("https://kodikplayer.com" + post_link, data=form, proxies=proxies, timeout=30).json()
    links = resp["links"]
    max_q = max(int(k) for k in links)
    return links[str(max_q)][0]["src"]


# 1) translation-specific player link from API
r = requests.post(
    "https://kodik-api.com/search",
    data={
        "token": token,
        "shikimori_id": SHIKIMORI_ID,
        "translation_id": TRANSLATION_ID,
        "limit": "1",
    },
    proxies=proxies,
    timeout=30,
)
player_link = "https:" + r.json()["results"][0]["link"]
print("player", player_link)

html = requests.get(player_link, proxies=proxies, timeout=30).text
url_params = extract_json_after(html, "urlParams")

# 2) find episode in serial-series-box (not translations box!)
box_pos = html.find("serial-series-box")
select_pos = html.find("<select", box_pos)
select_end = html.find("</select>", select_pos)
series_block = html[select_pos:select_end]

ep_id = ep_hash = ""
for m in re.finditer(r"<option\s+([^>]+)>", series_block):
    attrs = m.group(1)
    if attr(attrs, "value") == str(EPISODE):
        ep_id = attr(attrs, "data-id")
        ep_hash = attr(attrs, "data-hash")
        break
print("episode", EPISODE, ep_id, ep_hash[:16] if ep_hash else "")

# 3) episode player page
ep_url = f"https://kodikplayer.com/serial/{ep_id}/{ep_hash}/720p?min_age=16&first_url=false"
page = requests.get(ep_url, proxies=proxies, timeout=30).text
src = fetch_stream_from_page(page, url_params)
print("stream", src[:100])
print("OK" if "m3u8" in src or "hls" in src else "FAIL")