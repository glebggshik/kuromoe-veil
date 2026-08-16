"""Test hybrid Kodik: proxy for API/embed, direct for episode page."""
import os
import json
import re
import time
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
PX = {
    "http": os.environ.get("KODIK_PROXY", ""),
    "https": os.environ.get("KODIK_PROXY", ""),
}
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0"}


def get_with_retry(url, proxies, referer="", tries=5):
    headers = dict(UA)
    if referer:
        headers["Referer"] = referer
    for i in range(tries):
        try:
            r = requests.get(url, headers=headers, proxies=proxies, timeout=60)
            if r.status_code == 200 and len(r.text) > 2000 and "urlParams" in r.text:
                return r.text
            print(f"  try {i+1}: status={r.status_code} len={len(r.text)}")
        except Exception as e:
            print(f"  try {i+1}: err {e}")
        time.sleep(2 * (i + 1))
    return ""


def attr(attrs, name):
    m = re.search(name + r'="([^"]*)"', attrs)
    return m.group(1) if m else ""


def extract_json(text, marker):
    start = text.find(marker)
    if start < 0:
        return None
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


# API player link
r = requests.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=PX,
    timeout=60,
)
api_link = "https:" + r.json()["results"][0]["link"]
print("API link", api_link)

# Embed (generic)
embed_url = (
    f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
    f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D16498"
    f"&token={TOKEN}&shikimoriID=16498"
)
embed_link = requests.get(embed_url, proxies=PX, timeout=60).json()["link"]
if embed_link.startswith("//"):
    embed_link = "https:" + embed_link
print("Embed link", embed_link)

for label, link in [("api", api_link), ("embed", embed_link)]:
    print(f"\n=== {label} ===")
    html = get_with_retry(link, PX)
    if not html:
        print("FAIL proxy fetch")
        continue
    url_params = extract_json(html, "urlParams")
    print("urlParams ok")

    box = html.find("serial-translations-box")
    sp = html.find("<select", box)
    se = html.find("</select>", sp)
    block = html[sp:se]
    media_id = media_hash = ""
    for m in re.finditer(r"<option\s+([^>]+)>", block):
        a = m.group(1)
        if attr(a, "data-id") == "609":
            media_id = attr(a, "data-media-id")
            media_hash = attr(a, "data-media-hash")
            break
    print("translation media", media_id, media_hash[:16] if media_hash else "")

    ep_url = (
        f"https://kodikplayer.com/serial/{media_id}/{media_hash}/720p"
        f"?min_age=16&first_url=false&season=1&episode=1"
    )
    for mode, px in [("direct", None), ("proxy", PX)]:
        page = get_with_retry(ep_url, px, referer=link, tries=3)
        print(f"  episode [{mode}] len", len(page))
        if not page:
            continue
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
        if not script_url:
            continue
        hc = ""
        for i, m in enumerate(scripts):
            if i == 4:
                hc = m.group(2)
                break
        vt = hc[hc.find(".type = '") + 9 :].split("'")[0] if ".type" in hc else ""
        vh = hc[hc.find(".hash = '") + 9 :].split("'")[0] if ".hash" in hc else ""
        vi = hc[hc.find(".id = '") + 7 :].split("'")[0] if ".id" in hc else ""
        script = requests.get(
            "https://kodikplayer.com" + script_url,
            headers={**UA, "Referer": ep_url},
            proxies=px,
            timeout=60,
        ).text
        ap = script.find("$.ajax")
        cp = script.find("cache:!1")
        post = b64decode(script[ap + 30 : cp - 3]).decode() if ap >= 0 and cp > ap else "/ftor"
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
        resp = requests.post(
            "https://kodikplayer.com" + post,
            data=form,
            headers=UA,
            proxies=px,
            timeout=60,
        )
        print(f"  POST [{mode}]", resp.status_code)
        if resp.status_code == 200:
            j = resp.json()
            mq = max(int(k) for k in j["links"])
            print("  STREAM OK", j["links"][str(mq)][0]["src"][:100])
            break