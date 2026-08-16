#!/usr/bin/env python3
import os
import html as html_mod
import json
import re
import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36",
    "Referer": "https://animego.org/",
    "X-Requested-With": "XMLHttpRequest",
}

r = requests.get("https://animego.org/player/2280", headers=H, proxies=proxies, timeout=45)
body = r.text.strip()
if body.startswith("{"):
    body = html_mod.unescape(json.loads(body)["data"]["content"])

for m in re.finditer(r"<button\s+([^>]+)>", body):
    attrs = m.group(1)
    if 'data-provider-title="CVH"' not in attrs:
        continue
    label = re.search(r'data-translation-title="([^"]*)"', attrs)
    embed = re.search(r'data-player="([^"]*)"', attrs)
    pt = re.search(r'data-ptranslation="([^"]*)"', attrs)
    print("label:", (label.group(1) if label else "").replace(" (ошибка)", ""))
    print("  ptranslation:", pt.group(1) if pt else "")
    print("  embed:", (embed.group(1) if embed else "")[:100])

# playlist for shiki 38000
pl = requests.get(
    "https://plapi.cdnvideohub.com/api/v1/player/sv/playlist?pub=747&aggr=mali&id=38000",
    headers={"User-Agent": H["User-Agent"], "Accept": "application/json"},
    timeout=30,
).json()
studios = sorted({x["voiceStudio"] for x in pl["items"] if x["episode"] == 1})
print("CVH studios ep1:", studios)