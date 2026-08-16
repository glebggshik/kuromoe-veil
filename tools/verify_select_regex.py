import re
import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()
link = "https:" + requests.get(
    f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
    f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D16498"
    f"&token={token}&shikimoriID=16498",
    proxies=proxies,
    timeout=30,
).json()["link"].lstrip(":")
html = requests.get(link, proxies=proxies, timeout=30).text

m = re.search(r'serial-translations-box"[^>]*>\s*<select([\s\S]*?)</select>', html)
block = "<select" + m.group(1) + "</select>"
opts = re.findall(r"<option\s+([^>]+)>([^<]*)", block)
print("translations", len(opts), [n.strip()[:25] for _, n in opts[:4]])