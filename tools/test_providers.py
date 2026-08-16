#!/usr/bin/env python3
import os
import html
import json
import re
from collections import Counter

import requests

PROXY = os.environ.get("KODIK_PROXY", "")
proxies = {"http": PROXY, "https": PROXY}
H = {
    "User-Agent": "Mozilla/5.0",
    "X-Requested-With": "XMLHttpRequest",
    "Referer": "https://animego.org/",
}

for aid in ["294", "11", "126", "1635", "2430", "2203", "1570", "863"]:
    r = requests.get(f"https://animego.org/player/{aid}", headers=H, proxies=proxies, timeout=45)
    body = r.text
    if body.strip().startswith("{"):
        body = html.unescape(json.loads(body)["data"]["content"])
    providers = re.findall(r'data-provider-title="([^"]+)"', body)
    print(aid, Counter(providers), "has_provider", 'id="provider"' in body)