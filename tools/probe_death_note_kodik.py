import os
import json
import re
import requests
from base64 import b64decode

proxy = os.environ.get("KODIK_PROXY", "")
proxies = {"http": proxy, "https": proxy}
token = "56a768d08f43091901c44b54fe970049"
sid = "1535"  # Death Note


def extract_json(text, marker):
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


def attr(attrs, name):
    m = re.search(name + r'="([^"]*)"', attrs)
    return m.group(1) if m else ""


r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": sid, "with_material_data": "false", "limit": "50"},
    proxies=proxies,
    timeout=30,
)
results = r.json().get("results", [])
tr = next((x for x in results if "LKM" in x["translation"]["title"]), results[0])
tid = str(tr["translation"]["id"])
print("translation", tr["translation"]["title"], tid)

r2 = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": sid, "translation_id": tid, "limit": "1"},
    proxies=proxies,
    timeout=30,
)
link = "https:" + r2.json()["results"][0]["link"]
print("player", link)
html = requests.get(link, proxies=proxies, timeout=30).text
url_params = extract_json(html, "urlParams")

bp = html.find("serial-series-box")
ep_id = ep_hash = ""
if bp >= 0:
    sp = html.find("<select", bp)
    se = html.find("</select>", sp)
    block = html[sp:se]
    for m in re.finditer(r"<option\s+([^>]+)>", block):
        attrs = m.group(1)
        if attr(attrs, "value") == "1":
            ep_id = attr(attrs, "data-id")
            ep_hash = attr(attrs, "data-hash")
            break
print("series-box ep1", ep_id, (ep_hash or "")[:24])

if ep_id:
    ep_url = f"https://kodikplayer.com/serial/{ep_id}/{ep_hash}/720p?min_age=16&first_url=false"
else:
    m = re.search(r"kodikplayer.com/serial/(\d+)/([a-f0-9]+)/", link)
    ep_url = (
        f"https://kodikplayer.com/serial/{m.group(1)}/{m.group(2)}/720p"
        f"?min_age=16&first_url=false&season=1&episode=1"
    )
print("ep_url", ep_url)

page = requests.get(ep_url, proxies=proxies, timeout=30).text
scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page))
script_url = ""
idx = 0
for m in scripts:
    sm = re.search(r'src="([^"]+)"', m.group(1))
    if sm:
        if idx == 1:
            script_url = sm.group(1)
        idx += 1
if not script_url:
    mm = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
    script_url = mm.group(1) if mm else ""
print("script_url", script_url)

hash_c = scripts[4].group(2) if len(scripts) > 4 else ""
video_type = re.search(r"\.type = '([^']+)'", hash_c)
video_hash = re.search(r"\.hash = '([^']+)'", hash_c)
video_id = re.search(r"\.id = '([^']+)'", hash_c)
print("video meta", video_type and video_type.group(1), video_id and video_id.group(1))

script = requests.get("https://kodikplayer.com" + script_url, proxies=proxies, timeout=30).text
ap = script.find("$.ajax")
cp = script.find("cache:!1")
print("ajax", ap, "cache", cp)
for off in range(24, 40):
    try:
        pl = b64decode(script[ap + off : cp - 33]).decode()
        print("off", off, "post_link", repr(pl))
    except Exception as e:
        print("off", off, "err", e)

post_link = b64decode(script[ap + 30 : cp - 33]).decode()
form = {
    "hash": video_hash.group(1),
    "id": video_id.group(1),
    "type": video_type.group(1),
    "d": url_params["d"],
    "d_sign": url_params["d_sign"],
    "pd": url_params["pd"],
    "pd_sign": url_params["pd_sign"],
    "ref": "",
    "ref_sign": url_params["ref_sign"],
    "bad_user": "true",
    "cdn_is_working": "true",
}
resp = requests.post("https://kodikplayer.com" + post_link, data=form, proxies=proxies, timeout=30)
print("POST", "https://kodikplayer.com" + post_link)
print("status", resp.status_code, resp.text[:300])