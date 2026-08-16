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
    "Accept": "application/json",
}


def player_html(body: str) -> str:
    body = body.strip()
    if not body.startswith("{"):
        return body
    return html_mod.unescape(json.loads(body).get("data", {}).get("content", ""))


def cvh_hls_for_animego(animego_id: str, shiki_id: str):
    r = requests.get(
        f"https://animego.org/player/{animego_id}",
        headers={**H, "X-Requested-With": "XMLHttpRequest"},
        proxies=proxies,
        timeout=45,
    )
    content = player_html(r.text)
    embed = ""
    for m in re.finditer(r"<button\s+([^>]+)>", content):
        attrs = m.group(1)
        if 'data-provider-title="CVH"' not in attrs:
            continue
        em = re.search(r'data-player="([^"]*)"', attrs)
        if em:
            embed = em.group(1)
            break
    if not embed:
        print("no CVH on player", animego_id)
        return None
    if embed.startswith("//"):
        embed = "https:" + embed
    cvh_id_m = re.search(r"cdn-iframe/([^/]+)/", embed)
    cvh_id = cvh_id_m.group(1) if cvh_id_m else shiki_id
    pl = requests.get(
        f"https://plapi.cdnvideohub.com/api/v1/player/sv/playlist?pub=747&aggr=mali&id={cvh_id}",
        headers=H,
        timeout=30,
    ).json()
    vk = pl["items"][0]["vkId"]
    vid = requests.get(
        f"https://plapi.cdnvideohub.com/api/v1/player/sv/video/{vk}", headers=H, timeout=30
    ).json()
    return vid["sources"]["hlsUrl"]


def probe(url: str, headers: dict, use_proxy: bool):
    kw = {"headers": headers, "timeout": 20, "allow_redirects": True}
    if use_proxy:
        kw["proxies"] = proxies
    r = requests.get(url, **kw)
    print(
        f"  hdr={headers!r} proxy={use_proxy} -> status={r.status_code} "
        f"len={len(r.content)}"
    )


for animego_id, shiki_id, name in [
    ("2280", "38000", "Kimetsu movie"),
    ("2430", "52991", "Frieren"),
]:
    print(f"\n=== {name} animego={animego_id} ===")
    url = cvh_hls_for_animego(animego_id, shiki_id)
    if not url:
        continue
    print("hls:", url[:100], "...")
    cases = [
        {},
        {"Range": "bytes=0-1"},
        {"User-Agent": H["User-Agent"]},
        {"User-Agent": H["User-Agent"], "Range": "bytes=0-1"},
    ]
    for hdr in cases:
        for use_proxy in (False, True):
            probe(url, hdr, use_proxy)