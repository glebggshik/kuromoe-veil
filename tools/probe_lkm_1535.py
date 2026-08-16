import os
import json
import re
import requests

proxy = os.environ.get("KODIK_PROXY", "")
proxies = {"http": proxy, "https": proxy}
token = "56a768d08f43091901c44b54fe970049"

r = requests.post(
    "https://kodik-api.com/search",
    data={"token": token, "shikimori_id": "1535", "with_material_data": "false", "limit": "100"},
    proxies=proxies,
    timeout=30,
)
for x in r.json().get("results", []):
    t = x["translation"]
    print(t["id"], t["title"], "ep", x.get("last_episode"))

print("--- LKM search ---")
for x in r.json().get("results", []):
    t = x["translation"]
    if "LKM" in t["title"].upper() or "ЛКМ" in t["title"]:
        tid = str(t["id"])
        r2 = requests.post(
            "https://kodik-api.com/search",
            data={"token": token, "shikimori_id": "1535", "translation_id": tid, "limit": "1"},
            proxies=proxies,
            timeout=30,
        )
        link = "https:" + r2.json()["results"][0]["link"]
        print("LKM id", tid, "link", link)
        html = requests.get(link, proxies=proxies, timeout=30).text
        print("html len", len(html), "geo", "запрещено" in html)
        for cls in ["serial-series-box", "serial-seasons-box", "serial-translations-box"]:
            pos = html.find(cls)
            print(cls, "at", pos)
            if pos >= 0:
                chunk = html[pos : pos + 2500]
                opts = re.findall(r"<option\s+([^>]+)>([^<]*)", chunk)
                print("  options", len(opts))
                for attrs, name in opts[:4]:
                    print("   ", repr(name.strip()[:40]), attrs[:90])
        break
else:
    print("LKM not found")