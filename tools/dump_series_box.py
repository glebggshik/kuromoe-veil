import re
import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()

r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    proxies=proxies,
    timeout=30,
)
link = "https:" + r.json()["results"][0]["link"]
html = requests.get(link, proxies=proxies, timeout=30).text

for cls in ["serial-series-box", "serial-seasons-box", "serial-translations-box"]:
    pos = html.find(cls)
    if pos < 0:
        print(cls, "MISSING")
        continue
    end = html.find("</div>", pos + len(cls) + 500)  # rough
    chunk = html[pos : pos + 3000]
    print("\n===", cls, "===")
    opts = re.findall(r"<option\s+([^>]+)>([^<]*)", chunk)
    print("options in first 3000 chars:", len(opts))
    for attrs, name in opts[:6]:
        print(" ", name.strip(), attrs[:100])

# search episode 1 with selected
for m in re.finditer(r'<option\s+([^>]*value="1"[^>]*)>([^<]*)', html):
    attrs, name = m.group(1), m.group(2).strip()
    if "серия" in name.lower() or "серия" in name:
        if "selected" in attrs or "data-id" in attrs:
            print("ep candidate:", name, attrs[:120])