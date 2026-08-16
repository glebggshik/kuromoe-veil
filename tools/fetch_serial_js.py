import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0 Chrome/120.0.0.0"
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
player = "https:" + r.json()["results"][0]["link"]
html = s.get(player, timeout=30).text
m = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', html)
script_url = m.group(1)
script = s.get("https://kodikplayer.com" + script_url, timeout=30).text
out = r"C:\Users\eblan\Documents\anime_client_cpp\tools\app.serial.sample.js"
open(out, "w", encoding="utf-8").write(script)
print("saved", len(script), "to", out)
for kw in ["ftor", "ajax", "atob", "urlParams", "hash", "bad_user", "cdn_is"]:
    print(kw, script.count(kw))
ap = script.find("$.ajax")
print("ajax ctx", repr(script[ap : ap + 200]))