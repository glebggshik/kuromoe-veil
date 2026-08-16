import json
import re
import sys
import requests
from base64 import b64decode

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()


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
    start = idx + len(marker)
    end = text.find("'", start)
    return text[start:end]


def stream_from(html):
    url_params = extract_json_after(html, "urlParams")
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    src = re.search(r'src="([^"]+)"', scripts[1].group(1)).group(1)
    hc = scripts[4].group(2)
    vt = extract_quoted_after(hc, ".type = '")
    vh = extract_quoted_after(hc, ".hash = '")
    vid = extract_quoted_after(hc, ".id = '")
    print("  meta", vt, vid, vh[:12], "urlParams", bool(url_params))
    script = requests.get("https://kodikplayer.com" + src, proxies=proxies, timeout=30).text
    ap = script.find("$.ajax")
    cp = script.find("cache:!1")
    post = b64decode(script[ap + 30 : cp - 33]).decode()
    form = {
        "hash": vh, "id": vid, "type": vt,
        "d": url_params["d"], "d_sign": url_params["d_sign"],
        "pd": url_params["pd"], "pd_sign": url_params["pd_sign"],
        "ref": "", "ref_sign": url_params["ref_sign"],
        "bad_user": "true", "cdn_is_working": "true",
    }
    resp = requests.post("https://kodikplayer.com" + post, data=form, proxies=proxies, timeout=30)
    print("  post status", resp.status_code, "body", resp.text[:200])
    data = resp.json()
    links = data["links"]
    q = str(max(int(k) for k in links))
    return links[q][0]["src"]


r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=proxies,
    timeout=30,
)
link = "https:" + r.json()["results"][0]["link"]
m = re.search(r"/serial/(\d+)/([a-f0-9]+)/", link)
media_id, media_hash = m.group(1), m.group(2)

# Method A: episode URL with season/episode query (Python style)
ep_url = f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p?min_age=16&first_url=false&season=1&episode=1"
page = requests.get(ep_url, proxies=proxies, timeout=30).text
print("Method A episode query URL")
try:
    print("OK", stream_from(page)[:90])
except Exception as e:
    print("FAIL", e)

# Method B: direct episode id/hash from series select
ep_id, ep_hash = "102018", "8fed0e8f1d366fd9d291f9d73cfc2c27"
ep_url2 = f"https://kodikplayer.com/serial/{ep_id}/{ep_hash}/720p?min_age=16&first_url=false"
page2 = requests.get(ep_url2, proxies=proxies, timeout=30).text
print("\nMethod B direct episode id/hash")
try:
    print("OK", stream_from(page2)[:90])
except Exception as e:
    print("FAIL", e)

# Method C: use anime_parsers_ru
from anime_parsers_ru import KodikParser
p = KodikParser(token=token, proxy=config.get_kodik_proxy(), validate_token=False)
print("\nMethod C library")
try:
    url = p.get_m3u8_playlist_link("16498", "shikimori", 1, "609")
    print("OK", url[:90])
except Exception as e:
    print("FAIL", e)