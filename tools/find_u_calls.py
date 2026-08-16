import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0"
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
player = "https:" + r.json()["results"][0]["link"]
mid, mh = "3540", "da90374c3e2090414c9c9d4fbb6a16e0"
ep = f"https://kodikplayer.com/serial/{mid}/{mh}/720p?min_age=16&first_url=false&season=1&episode=1"
page = s.get(ep, headers={"Referer": player}, timeout=30).text
m = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
script = s.get("https://kodikplayer.com" + m.group(1), headers={"Referer": ep}, timeout=30).text

# find stream load call
for pat in [r"u\(vInfo", r"u\(\{type:", r"successCallback", r"loadVideo", r"getVideo", r"initPlayer"]:
    print(pat, len(re.findall(pat, script)))

idx = script.find("url:atob(\"L2Z0b3I=\")")
print("ftor ajax at", idx)
print(script[idx - 800 : idx + 400])