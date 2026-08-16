import re
import requests

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
print("size", len(page))
scripts = re.findall(r"<script([^>]*)>", page)
for i, attrs in enumerate(scripts):
    src = re.search(r'src="([^"]*)"', attrs)
    print(i, "src=", src.group(1) if src else "(inline)", "len=", len(re.search(r">([\s\S]*?)</script>", page).group(1)) if not src else 0)
# list all script src
for m in re.finditer(r'<script[^>]+src="([^"]+)"', page):
    print("SRC", m.group(1))
# check for app.video
for pat in ["app.serial", "app.video", "urlParams", "ftor", "L2Z0b3I"]:
    print(pat, page.find(pat))