import json
import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers.update(
    {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "application/json, text/javascript, */*; q=0.01",
        "Accept-Language": "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7",
    }
)
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
player = "https:" + r.json()["results"][0]["link"]
s.get(player, timeout=30)
mid, mh = "3540", "da90374c3e2090414c9c9d4fbb6a16e0"
ep = f"https://kodikplayer.com/serial/{mid}/{mh}/720p?min_age=16&first_url=false&season=1&episode=1"
page = s.get(ep, headers={"Referer": player}, timeout=30).text

def var(name):
    m = re.search(rf"var {name}\s*=\s*\"([^\"]*)\"", page)
    return m.group(1) if m else ""

hc = ""
for m in re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page):
    if ".type = '" in m.group(2):
        hc = m.group(2)
        break

def q(field):
    mk = f".{field} = '"
    idx = hc.find(mk)
    start = idx + len(mk)
    end = hc.find("'", start)
    return hc[start:end]

vinfo = {f: q(f) for f in ["type", "hash", "id", "link", "secret", "uid"]}
m_app = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
script = s.get("https://kodikplayer.com" + m_app.group(1), headers={"Referer": ep}, timeout=30).text
ap = script.find("$.ajax")
cp = script.find("cache:!1", ap)
post = b64decode(script[ap + 30 : cp - 3]).decode()

form = {
    "hash": vinfo["hash"],
    "id": vinfo["id"],
    "type": vinfo["type"],
    "d": var("domain"),
    "d_sign": var("d_sign"),
    "pd": var("pd"),
    "pd_sign": var("pd_sign"),
    "ref": var("ref"),
    "ref_sign": var("ref_sign"),
    "bad_user": "false",
    "cdn_is_working": "true",
}
for k in ["link", "secret", "uid"]:
    if vinfo[k]:
        form[k] = vinfo[k]

header_sets = [
    ("minimal", {"Referer": ep}),
    (
        "xhr",
        {
            "Referer": ep,
            "Origin": "https://kodikplayer.com",
            "X-Requested-With": "XMLHttpRequest",
        },
    ),
    (
        "full_browser",
        {
            "Referer": ep,
            "Origin": "https://kodikplayer.com",
            "X-Requested-With": "XMLHttpRequest",
            "Sec-Fetch-Dest": "empty",
            "Sec-Fetch-Mode": "cors",
            "Sec-Fetch-Site": "same-origin",
        },
    ),
]

for label, hdrs in header_sets:
    resp = s.post(
        "https://kodikplayer.com" + post,
        data=form,
        headers={**hdrs, "Content-Type": "application/x-www-form-urlencoded"},
        timeout=30,
    )
    print(label, resp.status_code, resp.headers.get("content-type", ""), resp.text[:150].replace("\n", " "))