import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers.update(
    {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept-Language": "ru-RU,ru;q=0.9",
    }
)
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_episodes": "true"},
    timeout=30,
)
ep_link = "https:" + r.json()["results"][0]["seasons"]["1"]["episodes"]["1"]
page = s.get(ep_link, timeout=30).text


def var(name):
    m = re.search(rf'var {name}\s*=\s*"([^"]*)"', page)
    return m.group(1) if m else ""


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
m_app = re.search(r'src="(/assets/js/app\.player_single\.[^"]+\.js)"', page)
script = s.get("https://kodikplayer.com" + m_app.group(1), headers={"Referer": ep_link}, timeout=30).text
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
resp = s.post(
    "https://kodikplayer.com" + post,
    data=form,
    headers={"Referer": ep_link, "Origin": "https://kodikplayer.com", "X-Requested-With": "XMLHttpRequest"},
    timeout=30,
)
print("seria POST", resp.status_code, len(resp.text), resp.headers.get("Server"))
if resp.status_code == 200:
    j = resp.json()
    mq = max(int(k) for k in j["links"])
    print("OK", j["links"][str(mq)][0]["src"][:150])