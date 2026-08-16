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
print("link", link)

html = requests.get(link, proxies=proxies, timeout=30).text
box = html.find("serial-translations-box")
select_pos = html.find("<select", box)
select_end = html.find("</select>", select_pos)
block = html[select_pos:select_end]
opts = re.findall(r"<option\s+([^>]+)>([^<]*)", block)
print("options", len(opts))
for attrs, name in opts:
    data_id = re.search(r'data-id="([^"]*)"', attrs)
    value = re.search(r'value="([^"]*)"', attrs)
    tr_type = re.search(r'data-translation-type="([^"]*)"', attrs)
    print(
        " data-id=", data_id.group(1) if data_id else "?",
        " value=", value.group(1) if value else "?",
        " type=", tr_type.group(1) if tr_type else "?",
        " name=", name.strip()[:60],
    )