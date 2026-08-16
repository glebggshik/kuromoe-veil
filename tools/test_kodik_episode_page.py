import json
import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0"}

s = requests.Session()
s.headers.update(UA)
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
player = "https:" + r.json()["results"][0]["link"]
html = s.get(player, timeout=30).text

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

url_params = extract_json(html, "urlParams")
mid = mhash = ""
m = re.search(r"serial/(\d+)/([a-f0-9]+)", player)
mid, mhash = m.group(1), m.group(2)
print("from link", mid, mhash[:16])

ep_url = (
    f"https://kodikplayer.com/serial/{mid}/{mhash}/720p"
    f"?min_age=16&first_url=false&season=1&episode=1"
)
page = s.get(ep_url, headers={"Referer": player}, timeout=30).text
print("ep len", len(page), "geo", "запрещено" in page)
ep_params = extract_json(page, "urlParams")
print("ep urlParams", bool(ep_params))

scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page))
print("scripts with src:")
for i, sm in enumerate(scripts):
    src = re.search(r'src="([^"]+)"', sm.group(1))
    if src:
        print(" ", i, src.group(1)[:90])

script_url = ""
m_app = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
if m_app:
    script_url = m_app.group(1)
else:
    idx = 0
    for sm in scripts:
        src = re.search(r'src="([^"]+)"', sm.group(1))
        if src:
            if idx == 1:
                script_url = src.group(1)
                break
            idx += 1

hc = ""
for i, sm in enumerate(scripts):
    if ".type = '" in sm.group(2):
        hc = sm.group(2)
        break

def qafter(text, marker):
    idx = text.find(marker)
    start = idx + len(marker)
    end = text.find("'", start)
    return text[start:end]

vt, vh, vi = qafter(hc, ".type = '"), qafter(hc, ".hash = '"), qafter(hc, ".id = '")
print("script", script_url)
print("video", vt, vi, vh[:20] if vh else "")

script = s.get("https://kodikplayer.com" + script_url, headers={"Referer": ep_url}, timeout=30).text
ap = script.find("$.ajax")
cp = script.find("cache:!1")
post = b64decode(script[ap + 30 : cp - 3]).decode()
print("post", repr(post))

form = {
    "hash": vh,
    "id": vi,
    "type": vt,
    "d": url_params["d"],
    "d_sign": url_params["d_sign"],
    "pd": url_params["pd"],
    "pd_sign": url_params["pd_sign"],
    "ref": "",
    "ref_sign": url_params["ref_sign"],
    "bad_user": "true",
    "cdn_is_working": "true",
}
for label, params in [("parent", url_params), ("episode", ep_params)]:
    if not params:
        continue
    form = {
        "hash": vh,
        "id": vi,
        "type": vt,
        "d": params["d"],
        "d_sign": params["d_sign"],
        "pd": params["pd"],
        "pd_sign": params["pd_sign"],
        "ref": "",
        "ref_sign": params["ref_sign"],
        "bad_user": "true",
        "cdn_is_working": "true",
    }
    resp = s.post("https://kodikplayer.com" + post, data=form, timeout=30)
    print(f"POST [{label}]", resp.status_code, resp.text[:150])
    if resp.status_code == 200:
        j = resp.json()
        mq = max(int(k) for k in j["links"])
        print("OK", j["links"][str(mq)][0]["src"][:120])
        break