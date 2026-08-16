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


def stream_from(html, url_params):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    src = re.search(r'src="([^"]+)"', scripts[1].group(1)).group(1)
    hc = scripts[4].group(2)
    vt = extract_quoted_after(hc, ".type = '")
    vh = extract_quoted_after(hc, ".hash = '")
    vid = extract_quoted_after(hc, ".id = '")
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
    resp = requests.post("https://kodikplayer.com" + post, data=form, proxies=proxies, timeout=30).json()
    links = resp["links"]
    q = str(max(int(k) for k in links))
    return links[q][0]["src"]


r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=proxies,
    timeout=30,
)
item = r.json()["results"][0]
link = "https:" + item["link"]
# parse media id/hash from link: /serial/3540/hash/720p
m = re.search(r"/serial/(\d+)/([a-f0-9]+)/", link)
media_id, media_hash = m.group(1), m.group(2)
print("media", media_id, media_hash)

embed_html = requests.get(link, proxies=proxies, timeout=30).text
url_params = extract_json_after(embed_html, "urlParams")

for ep in [1, 2]:
    ep_url = (
        f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p"
        f"?min_age=16&first_url=false&season=1&episode={ep}"
    )
    page = requests.get(ep_url, proxies=proxies, timeout=30).text
    print(f"ep{ep} len={len(page)} scripts={len(re.findall(r'<script', page))}")
    try:
        src = stream_from(page, url_params)
        print(f"ep{ep} OK", src[:90])
    except Exception as e:
        print(f"ep{ep} FAIL", e)