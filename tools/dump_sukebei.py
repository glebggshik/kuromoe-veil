#!/usr/bin/env python3
import re
import requests

r = requests.get(
    "https://sukebei.nyaa.si/",
    params={"f": 0, "c": "0_0", "q": "Bible Black", "s": "seeders", "o": "desc"},
    headers={"User-Agent": "Mozilla/5.0"},
    timeout=30,
)
print("status", r.status_code, "len", len(r.text))
# save snippet around first magnet
idx = r.text.find("magnet:")
if idx >= 0:
    print(r.text[max(0, idx - 400): idx + 200])
# try row-based parse
rows = re.findall(r"<tr class=\"(?:default|success|danger)\">(.*?)</tr>", r.text, re.S)
print("rows", len(rows))
if rows:
    row = rows[0]
    title = re.search(r"class=\"torrent-title\".*?>(.*?)</td>", row, re.S)
    magnet = re.search(r'href="(magnet:[^"]+)"', row)
    seeders = re.search(r'<td class="text-center">(\d+)</td>\s*<td class="text-center">(\d+)</td>\s*<td class="text-center">(\d+)</td>', row)
    if title:
        t = re.sub(r"<[^>]+>", "", title.group(1)).strip()
        print("title:", t[:100])
    if magnet:
        print("magnet:", magnet.group(1)[:80])
    if seeders:
        print("seeders/leechers/dl:", seeders.groups())