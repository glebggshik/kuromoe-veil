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
    "Accept-Language": "ru-RU,ru;q=0.9",
    "Accept": "application/json",
    "X-Requested-With": "XMLHttpRequest",
}
CVH_BASE = "https://plapi.cdnvideohub.com/api/v1/player/sv"
PUB = "747"
AGGR = "mali"


def player_html(body: str) -> str:
    body = body.strip()
    if not body.startswith("{"):
        return body
    data = json.loads(body)
    return html_mod.unescape(data.get("data", {}).get("content", ""))


def extract_cvh_id(embed: str) -> str:
    m = re.search(r"cdn-iframe/([^/]+)/", embed)
    return m.group(1) if m else ""


animego_id = "2430"
r = requests.get(f"https://animego.org/player/{animego_id}", headers=H, proxies=proxies, timeout=45)
content = player_html(r.text)
print("player status", r.status_code, "html len", len(content))

voices = []
for m in re.finditer(r"<button\s+([^>]+)>", content):
    attrs = m.group(1)
    if 'data-provider-title="CVH"' not in attrs and "data-provider-title='CVH'" not in attrs:
        continue
    embed_m = re.search(r'data-player="([^"]*)"', attrs)
    embed = embed_m.group(1) if embed_m else ""
    if embed.startswith("//"):
        embed = "https:" + embed
    title_m = re.search(r'data-translation-title="([^"]*)"', attrs)
    label = (title_m.group(1) if title_m else "").replace(" (ошибка)", "")
    cvh_id = extract_cvh_id(embed)
    voices.append((label, cvh_id, embed[:80]))
    print(f"voice: {label!r} cvh_id={cvh_id!r} embed={embed[:90]!r}")

if not voices:
    print("NO CVH voices")
    raise SystemExit(1)

label, cvh_id, _ = voices[0]
playlist_url = f"{CVH_BASE}/playlist?pub={PUB}&aggr={AGGR}&id={cvh_id}"
print("\nplaylist url:", playlist_url)

for use_proxy, tag in [(False, "direct"), (True, "proxy")]:
    kw = {"headers": H, "timeout": 45}
    if use_proxy:
        kw["proxies"] = proxies
    pr = requests.get(playlist_url, **kw)
    print(f"\n=== playlist {tag} -> {pr.status_code} len={len(pr.text)} ===")
    print("first 500:", pr.text[:500])
    try:
        data = pr.json()
        if isinstance(data, list):
            print(f"JSON array, items={len(data)}")
            if data:
                print("first item keys:", data[0].keys())
                print("first item:", json.dumps(data[0], ensure_ascii=False)[:300])
        elif isinstance(data, dict):
            print(f"JSON object keys: {list(data.keys())}")
    except Exception as e:
        print("json parse error:", e)