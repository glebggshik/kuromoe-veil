import json
import re
import requests
from base64 import b64decode
from urllib.parse import unquote

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
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
    if not m:
        m = re.search(rf"{name}\s*=\s*\"([^\"]*)\"", page)
    return m.group(1) if m else ""

start = page.find("urlParams")
brace = page.find("{", start)
depth = 0
for i in range(brace, len(page)):
    if page[i] == "{":
        depth += 1
    elif page[i] == "}":
        depth -= 1
        if depth == 0:
            url_params = json.loads(page[brace : i + 1])
            break

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

vinfo = {f: q(f) for f in ["type", "hash", "id"]}
m_app = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
script = s.get("https://kodikplayer.com" + m_app.group(1), headers={"Referer": ep}, timeout=30).text
ap = script.find("$.ajax")
cp = script.find("cache:!1", ap)
post = b64decode(script[ap + 30 : cp - 3]).decode()

forms = {
    "empty_ref_urlparams": {
        "d": url_params["d"],
        "d_sign": url_params["d_sign"],
        "pd": url_params["pd"],
        "pd_sign": url_params["pd_sign"],
        "ref": "",
        "ref_sign": url_params["ref_sign"],
    },
    "encoded_ref": {
        "d": url_params["d"],
        "d_sign": url_params["d_sign"],
        "pd": url_params["pd"],
        "pd_sign": url_params["pd_sign"],
        "ref": url_params.get("ref", ""),
        "ref_sign": url_params["ref_sign"],
    },
    "decoded_ref_inline": {
        "d": var("domain") or url_params["d"],
        "d_sign": var("d_sign") or url_params["d_sign"],
        "pd": var("pd") or url_params["pd"],
        "pd_sign": var("pd_sign") or url_params["pd_sign"],
        "ref": var("ref"),
        "ref_sign": var("ref_sign") or url_params["ref_sign"],
    },
}

for label, p in forms.items():
    form = {
        **p,
        "hash": vinfo["hash"],
        "id": vinfo["id"],
        "type": vinfo["type"],
        "bad_user": "false",
        "cdn_is_working": "true",
    }
    resp = s.post(
        "https://kodikplayer.com" + post,
        data=form,
        headers={"Referer": ep},
        timeout=30,
    )
    print(label, resp.status_code, "ref=", repr(form["ref"][:80]))
    if resp.status_code == 200:
        j = resp.json()
        print("OK", list(j.keys()))
    else:
        print(resp.text[:100])