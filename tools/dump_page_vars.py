import json
import re
import requests

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

for var in ["domain", "d_sign", "pd", "pd_sign", "ref", "ref_sign", "uid", "translationId"]:
    m = re.search(rf"var {var}\s*=\s*\"([^\"]*)\"", page)
    if not m:
        m = re.search(rf"{var}\s*=\s*\"([^\"]*)\"", page)
    print(var, "=", m.group(1) if m else "NOT FOUND")

start = page.find("urlParams")
brace = page.find("{", start)
depth = 0
for i in range(brace, len(page)):
    if page[i] == "{":
        depth += 1
    elif page[i] == "}":
        depth -= 1
        if depth == 0:
            print("urlParams", json.loads(page[brace : i + 1]))
            break

m = re.search(r"vInfo\s*=\s*(\{[^;]+\})", page)
if m:
    print("vInfo raw", m.group(1)[:300])

# search userInfo
for kw in ["userInfo", "playerSettings.badUser", "playerSettings.cdnIsWorking"]:
    idx = page.find(kw)
    print(kw, "at", idx)
    if idx >= 0:
        print(page[idx : idx + 200])