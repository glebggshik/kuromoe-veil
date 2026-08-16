"""Kodik flow без прокси — как на домашней сети."""
import json
import re
import sys
from base64 import b64decode

import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
SHIKIMORI = "16498"
TRANSLATION = "609"
EPISODE = 1
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0"}


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


# 1) API link for translation (C++ path)
r = requests.post(
    "https://kodik-api.com/search",
    data={
        "token": TOKEN,
        "shikimori_id": SHIKIMORI,
        "translation_id": TRANSLATION,
        "limit": "1",
    },
    headers=UA,
    timeout=30,
)
player_link = "https:" + r.json()["results"][0]["link"]
print("API player", player_link)

html = requests.get(player_link, headers=UA, timeout=30).text
print("main len", len(html), "geo", "запрещено" in html)
parent_params = extract_json_after(html, "urlParams")
print("parent urlParams", bool(parent_params))

# Python parser path: translation box -> episode page
box = html.find("serial-translations-box")
sp = html.find("<select", box)
se = html.find("</select>", sp)
block = html[sp:se]
media_id = media_hash = ""
for m in re.finditer(r"<option\s+([^>]+)>", block):
    a = m.group(1)
    if attr(a, "data-id") == TRANSLATION or attr(a, "value") == TRANSLATION:
        media_id = attr(a, "data-media-id")
        media_hash = attr(a, "data-media-hash")
        break
print("translation media", media_id, (media_hash or "")[:16])

ep_url = (
    f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p"
    f"?min_age=16&first_url=false&season=1&episode={EPISODE}"
)
page = requests.get(ep_url, headers={**UA, "Referer": player_link}, timeout=30).text
print("episode len", len(page))
ep_params = extract_json_after(page, "urlParams")
print("episode urlParams", bool(ep_params))

scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page))
script_url = ""
idx = 0
for m in scripts:
    src = attr(m.group(1), "src")
    if src:
        if idx == 1:
            script_url = src
            break
        idx += 1
print("script", script_url)

hc = ""
for i, m in enumerate(scripts):
    if i == 4:
        hc = m.group(2)
        break
if ".type" not in hc:
    for m in scripts:
        if ".type" in m.group(2):
            hc = m.group(2)
            break

def qafter(text, marker):
    idx = text.find(marker)
    if idx < 0:
        return ""
    start = idx + len(marker)
    end = text.find("'", start)
    return text[start:end] if end >= 0 else ""

vt, vh, vi = qafter(hc, ".type = '"), qafter(hc, ".hash = '"), qafter(hc, ".id = '")
print("video", vt, vi, (vh or "")[:16])

script = requests.get(
    "https://kodikplayer.com" + script_url,
    headers={**UA, "Referer": ep_url},
    timeout=30,
).text
ap = script.find("$.ajax")
cp = script.find("cache:!1")
post = b64decode(script[ap + 30 : cp - 3]).decode() if ap >= 0 and cp > ap else "/ftor"
print("post_link", repr(post))

for label, params in [("parent", parent_params), ("episode", ep_params)]:
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
    resp = requests.post(
        "https://kodikplayer.com" + post,
        data=form,
        headers=UA,
        timeout=30,
    )
    print(f"POST [{label}]", resp.status_code, resp.text[:120])
    if resp.status_code == 200:
        j = resp.json()
        mq = max(int(k) for k in j["links"])
        print("OK", j["links"][str(mq)][0]["src"][:100])
        sys.exit(0)

print("FAIL both")
sys.exit(1)