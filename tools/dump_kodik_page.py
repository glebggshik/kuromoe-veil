import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
r = requests.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1"},
    timeout=30,
)
link = "https:" + r.json()["results"][0]["link"]
html = requests.get(link, timeout=30).text
print("len", len(html))

for box_name in ["serial-translations-box", "serial-series-box"]:
    box = html.find(box_name)
    print(f"\n=== {box_name} at {box} ===")
    if box < 0:
        continue
    sp = html.find("<select", box)
    se = html.find("</select>", sp)
    block = html[sp:se]
    opts = re.findall(r"<option\s+([^>]+)>", block)
    print("options", len(opts))
    for a in opts[:15]:
        def g(n):
            m = re.search(n + r'="([^"]*)"', a)
            return m.group(1) if m else ""

        print(
            "val", g("value"),
            "data-id", g("data-id"),
            "media-id", g("data-media-id"),
            "hash", g("data-hash")[:12],
            "mhash", g("data-media-hash")[:12],
        )