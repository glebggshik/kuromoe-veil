import json
import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"


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


r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
player = "https:" + r.json()["results"][0]["link"]
parent = s.get(player, timeout=30).text
parent_params = extract_json(parent, "urlParams")

mid, mh = "3540", "da90374c3e2090414c9c9d4fbb6a16e0"
ep = f"https://kodikplayer.com/serial/{mid}/{mh}/720p?min_age=16&first_url=false&season=1&episode=1"
page = s.get(ep, headers={"Referer": player}, timeout=30).text
episode_params = extract_json(page, "urlParams")

hc = ""
for m in re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page):
    if ".type = '" in m.group(2):
        hc = m.group(2)
        break


def q(field):
    mk = f".{field} = '"
    idx = hc.find(mk)
    if idx < 0:
        return ""
    start = idx + len(mk)
    end = hc.find("'", start)
    return hc[start:end]


vinfo = {f: q(f) for f in ["type", "hash", "id"]}
print("vInfo", vinfo)

m_app = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
script = s.get("https://kodikplayer.com" + m_app.group(1), headers={"Referer": ep}, timeout=30).text
ap = script.find("$.ajax")
cp = script.find("cache:!1", ap)
post = b64decode(script[ap + 30 : cp - 3]).decode()
print("post path", post)


def make_form(params):
    return {
        "hash": vinfo["hash"],
        "id": vinfo["id"],
        "type": vinfo["type"],
        "d": params["d"],
        "d_sign": params["d_sign"],
        "pd": params["pd"],
        "pd_sign": params["pd_sign"],
        "ref": params.get("ref", ""),
        "ref_sign": params["ref_sign"],
        "bad_user": "true",
        "cdn_is_working": "true",
    }


for label, params in [
    ("parent", parent_params),
    ("episode", episode_params),
]:
    form = make_form(params)
    resp = s.post("https://kodikplayer.com" + post, data=form, headers={"Referer": ep}, timeout=30)
    print(f"POST [{label}]", resp.status_code, form.get("d"), form.get("ref", "")[:60])
    if resp.status_code == 200:
        j = resp.json()
        mq = max(int(k) for k in j["links"])
        print("OK", j["links"][str(mq)][0]["src"][:120])
    else:
        print(resp.text[:200])