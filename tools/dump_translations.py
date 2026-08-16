import os
import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
PX = {
    "http": os.environ.get("KODIK_PROXY", ""),
    "https": os.environ.get("KODIK_PROXY", ""),
}

embed_url = (
    f"https://kodik-api.com/get-player?title=Player&hasPlayer=false"
    f"&url=https%3A%2F%2Fkodikdb.com%2Ffind-player%3FshikimoriID%3D16498"
    f"&token={TOKEN}&shikimoriID=16498"
)
link = "https:" + requests.get(embed_url, proxies=PX, timeout=60).json()["link"]
print("link", link)
html = requests.get(link, proxies=PX, timeout=60).text
print("len", len(html), "urlParams", "urlParams" in html)

box = html.find("serial-translations-box")
sp = html.find("<select", box)
se = html.find("</select>", sp)
block = html[sp:se]
opts = re.findall(r"<option\s+([^>]+)>", block)
print("options", len(opts))
for a in opts[:12]:
    def g(name):
        m = re.search(name + r'="([^"]*)"', a)
        return m.group(1) if m else ""

    print("val", g("value"), "data-id", g("data-id"), "media-id", g("data-media-id"), "hash", g("data-media-hash")[:12])