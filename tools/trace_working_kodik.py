import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import config
from anime_parsers_ru import KodikParser

# fresh parser, no cache from video_source singleton
p = KodikParser(token=config.get_kodik_token(), proxy=config.get_kodik_proxy(), validate_token=False)
p.use_cache = True

link = p.get_embed_link("16498", "shikimori")
print("embed", link)

html = requests.get(link, proxies=p.proxies, timeout=30).text
print("embed html len", len(html), "geo", "запрещено" in html)

from bs4 import BeautifulSoup
soup = BeautifulSoup(html, "html.parser")
container = soup.find("div", {"class": "serial-translations-box"})
if container:
    sel = container.find("select")
    if sel:
        opts = sel.find_all("option")
        print("translations-box options", len(opts))
        for o in opts[:5]:
            print(" ", o.text.strip()[:40], "data-id", o.get("data-id"), "value", o.get("value"))

# monkeypatch requests.get to log episode fetch
orig_get = requests.get
def logged_get(url, *a, **kw):
    if "kodikplayer.com/serial/" in str(url) and "episode=" in str(url):
        print("EPISODE GET", url)
        r = orig_get(url, *a, **kw)
        print("  status", r.status_code, "len", len(r.text))
        return r
    return orig_get(url, *a, **kw)

requests.get = logged_get

try:
    url = p.get_m3u8_playlist_link("16498", "shikimori", 1, "609")
    print("RESULT", url[:100])
except Exception as e:
    print("ERROR", e)