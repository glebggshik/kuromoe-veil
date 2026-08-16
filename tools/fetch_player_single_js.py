import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0"
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_episodes": "true"},
    timeout=30,
)
ep_link = "https:" + r.json()["results"][0]["seasons"]["1"]["episodes"]["1"]
page = s.get(ep_link, timeout=30).text
m = re.search(r'src="(/assets/js/app\.player_single\.[^"]+\.js)"', page)
url = "https://kodikplayer.com" + m.group(1)
print("script", url)
script = s.get(url, headers={"Referer": ep_link}, timeout=30).text
print("size", len(script))
for kw in ["ftor", "L2Z0b3I", "atob", "bad_user", "cdn_is_working", "$.ajax"]:
    print(kw, script.count(kw))
ap = script.find("$.ajax")
if ap >= 0:
    cp = script.find("cache:!1", ap)
    if cp > ap:
        post = b64decode(script[ap + 30 : cp - 3]).decode()
        print("post path", post)
idx = script.find("bad_user:playerSettings.badUser")
print("builder", script[idx : idx + 400] if idx >= 0 else "not found")
open(r"C:\Users\eblan\Documents\anime_client_cpp\tools\app.player_single.sample.js", "w", encoding="utf-8").write(script)
print("saved")