"""Debug Kodik full flow — API + player via SOCKS5 proxy."""
import os
import json
import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
PX = {
    "http": os.environ.get("KODIK_PROXY", ""),
    "https": os.environ.get("KODIK_PROXY", ""),
}
UA = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
}


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


def script_src_at_tag_index(html, tag_index):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    idx = 0
    for m in scripts:
        src = attr(m.group(1), "src")
        if src:
            if idx == tag_index:
                return src
            idx += 1
    m = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', html)
    return m.group(1) if m else ""


def hash_container_from_html(html):
    scripts = list(re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", html))
    if len(scripts) > 4 and ".type = '" in scripts[4].group(2):
        return scripts[4].group(2)
    for m in scripts:
        if ".type = '" in m.group(2):
            return m.group(2)
    return ""


def extract_quoted_after(text, marker):
    idx = text.find(marker)
    if idx < 0:
        return ""
    start = idx + len(marker)
    end = text.find("'", start)
    return text[start:end] if end >= 0 else ""


def extract_post_link(script):
    for m in re.finditer(r'atob\s*\(\s*"([A-Za-z0-9+/=]+)"\s*\)', script):
        path = b64decode(m.group(1)).decode()
        if path.startswith("/") and len(path) >= 4:
            return path
    ap = script.find("$.ajax")
    cp = script.find("cache:!1")
    if ap >= 0 and cp > ap:
        # parser_kodik.py: [ajax+30 : cache-3]
        path = b64decode(script[ap + 30 : cp - 3]).decode()
        if path.startswith("/"):
            return path
    if "L2Z0b3I=" in script:
        return "/ftor"
    return ""


def try_post(post_link, form, label):
    resp = requests.post(
        "https://kodikplayer.com" + post_link,
        data=form,
        headers=UA,
        proxies=PX,
        timeout=60,
    )
    print(f"POST [{label}]", resp.status_code)
    if resp.status_code == 200:
        j = resp.json()
        if "links" in j:
            mq = max(int(k) for k in j["links"])
            print("OK stream", j["links"][str(mq)][0]["src"][:120])
            return True
        print("error json", j)
    else:
        print("body", resp.text[:200])
    return False


r = requests.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=PX,
    timeout=60,
)
link = "https:" + r.json()["results"][0]["link"]
print("player link", link)

html = requests.get(link, headers=UA, proxies=PX, timeout=60).text
print("main len", len(html), "geo", "запрещено" in html)
parent_params = extract_json(html, "urlParams")
print("parent urlParams", bool(parent_params))

m = re.search(r"serial/(\d+)/([a-f0-9]+)", link)
mid, mhash = m.group(1), m.group(2)
ep_url = (
    f"https://kodikplayer.com/serial/{mid}/{mhash}/720p"
    f"?min_age=16&first_url=false&season=1&episode=1"
)
page = requests.get(ep_url, headers={**UA, "Referer": link}, proxies=PX, timeout=60).text
print("episode len", len(page), "geo", "запрещено" in page)
ep_params = extract_json(page, "urlParams")
print("episode urlParams", bool(ep_params))

script_url = script_src_at_tag_index(page, 1)
print("script", script_url)
hc = hash_container_from_html(page)
vt = extract_quoted_after(hc, ".type = '")
vh = extract_quoted_after(hc, ".hash = '")
vi = extract_quoted_after(hc, ".id = '")
print("video", vt, vi, vh[:16] if vh else "")

script = requests.get(
    "https://kodikplayer.com" + script_url,
    headers={**UA, "Referer": ep_url},
    proxies=PX,
    timeout=60,
).text
post_link = extract_post_link(script)
print("post_link", repr(post_link))

if parent_params and all([vt, vh, vi, post_link]):
    form = {
        "hash": vh,
        "id": vi,
        "type": vt,
        "d": parent_params["d"],
        "d_sign": parent_params["d_sign"],
        "pd": parent_params["pd"],
        "pd_sign": parent_params["pd_sign"],
        "ref": "",
        "ref_sign": parent_params["ref_sign"],
        "bad_user": "true",
        "cdn_is_working": "true",
    }
    if not try_post(post_link, form, "parent urlParams") and ep_params:
        form.update(
            {
                "d": ep_params["d"],
                "d_sign": ep_params["d_sign"],
                "pd": ep_params["pd"],
                "pd_sign": ep_params["pd_sign"],
                "ref_sign": ep_params["ref_sign"],
            }
        )
        try_post(post_link, form, "episode urlParams")