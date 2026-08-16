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

print("link", link)
print("data-media-id count", html.count("data-media-id"))
print("data-id count", html.count("data-id"))

for pat in [
    r'data-media-id="(\d+)"[^>]*data-media-hash="([^"]+)"',
    r'data-id="(\d+)"[^>]*data-media-id="(\d+)"',
    r'"translation_id"\s*:\s*(\d+)',
]:
    ms = re.findall(pat, html)
    print(pat, "->", ms[:5])

# all option tags
opts = re.findall(r"<option\s+([^>]+)>([^<]*)", html)
print("total options", len(opts))
for attrs, name in opts:
    if "media" in attrs or "translation" in attrs or "609" in attrs:
        print(" ", name.strip(), attrs)