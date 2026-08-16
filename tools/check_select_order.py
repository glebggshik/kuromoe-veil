import re
import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config

proxies = {"http": config.get_kodik_proxy(), "https": config.get_kodik_proxy()}
token = config.get_kodik_token()

embed_url = (
    f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
    f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D16498"
    f"&token={token}&shikimoriID=16498"
)
link = requests.get(embed_url, proxies=proxies, timeout=30).json()["link"]
if link.startswith("//"):
    link = "https:" + link
html = requests.get(link, proxies=proxies, timeout=30).text

box_pos = html.find("serial-translations-box")
print("box_pos", box_pos)

# C++ logic: first select after box_pos
select_pos = html.find("<select", box_pos)
select_end = html.find("</select>", select_pos)
block = html[select_pos:select_end]
opts = re.findall(r"<option\s+([^>]+)>([^<]*)", block)
print("C++ first-select-after-box:", len(opts), "options")
for a, n in opts[:4]:
    print(" ", n.strip()[:30])

# Correct: select INSIDE serial-translations-box div
# find the div and its inner select
m = re.search(
    r'<div[^>]*class="[^"]*serial-translations-box[^"]*"[^>]*>([\s\S]*?)</div>\s*</div>',
    html,
)
if m:
    inner = m.group(1)
    sel = re.search(r"<select[\s\S]*?</select>", inner)
    if sel:
        opts2 = re.findall(r"<option\s+([^>]+)>([^<]*)", sel.group(0))
        print("inside translations-box div:", len(opts2), "options")
        for a, n in opts2[:4]:
            print(" ", n.strip()[:30], "data-id", re.search(r'data-id="(\d+)"', a).group(1) if re.search(r'data-id', a) else "?")

# show positions of key classes
for cls in ["serial-translations-box", "serial-seasons-box", "serial-series-box"]:
    print(cls, "at", html.find(cls))