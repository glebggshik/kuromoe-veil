import re
import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()

for label, get_link in [
    ("generic", lambda: requests.get(
        f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
        f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D16498"
        f"&token={token}&shikimoriID=16498",
        proxies=proxies,
        timeout=30,
    ).json()["link"]),
    ("api609", lambda: requests.post(
        "https://kodik-api.com/search",
        data={"token": token, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
        proxies=proxies,
        timeout=30,
    ).json()["results"][0]["link"]),
]:
    link = get_link()
    if link.startswith("//"):
        link = "https:" + link
    print("\n===", label, link)
    html = requests.get(link, proxies=proxies, timeout=30).text
    for cls in ["serial-translations-box", "movie-translations-box", "serial-seasons-box", "serial-series-box"]:
        pos = html.find(cls)
        print(cls, "found" if pos >= 0 else "MISSING")
        if pos < 0:
            continue
        snippet = html[pos : pos + 1200]
        opts = re.findall(r"<option\s+([^>]+)>([^<]*)", snippet)
        print("  options in box:", len(opts))
        for attrs, name in opts[:5]:
            print("   ", name.strip()[:40], attrs[:120])