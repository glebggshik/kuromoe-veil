#!/usr/bin/env python3
"""Simulate full CVH play flow: video API route vs HLS srcIp binding."""
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
    return html_mod.unescape(json.loads(body)["data"]["content"])


def get_cvh_id(animego_id: str) -> str:
    r = requests.get(
        f"https://animego.org/player/{animego_id}",
        headers={**H, "X-Requested-With": "XMLHttpRequest"},
        proxies=proxies,
        timeout=45,
    )
    content = player_html(r.text)
    for m in re.finditer(r"<button\s+([^>]+)>", content):
        attrs = m.group(1)
        if 'data-provider-title="CVH"' not in attrs:
            continue
        em = re.search(r'data-player="([^"]*)"', attrs)
        if em:
            embed = em.group(1)
            if embed.startswith("//"):
                embed = "https:" + embed
            cid = re.search(r"cdn-iframe/([^/]+)/", embed)
            return cid.group(1) if cid else ""
    return ""


def hls_for_route(cvh_id: str, use_proxy_for_api: bool):
    kw = {"headers": H, "timeout": 30}
    if use_proxy_for_api:
        kw["proxies"] = proxies
    pl = requests.get(
        f"https://plapi.cdnvideohub.com/api/v1/player/sv/playlist?pub=747&aggr=mali&id={cvh_id}",
        **kw,
    ).json()
    ep1 = [x for x in pl["items"] if x["episode"] == 1]
    vk = ep1[0]["vkId"]
    vid = requests.get(
        f"https://plapi.cdnvideohub.com/api/v1/player/sv/video/{vk}", **kw
    ).json()
    url = vid["sources"]["hlsUrl"]
    src_ip = re.search(r"srcIp=([^&]+)", url)
    return url, src_ip.group(1) if src_ip else "?"


def play_probe(url: str, use_proxy: bool, referer: str | None):
    headers = {"User-Agent": H["User-Agent"]}
    if referer:
        headers["Referer"] = referer
    kw = {"headers": headers, "timeout": 20}
    if use_proxy:
        kw["proxies"] = proxies
    r = requests.get(url, **kw)
    ok = r.status_code == 200 and "#EXTM3U" in r.text[:200]
    print(
        f"    play proxy={use_proxy} referer={bool(referer)} -> {r.status_code} "
        f"m3u8={ok} bytes={len(r.text)}"
    )
    if ok:
        base = url.rsplit("/", 1)[0] + "/"
        for line in r.text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            seg = line if line.startswith("http") else base + line
            skw = {"headers": headers, "timeout": 20}
            if use_proxy:
                skw["proxies"] = proxies
            sr = requests.get(seg, **skw)
            print(f"      seg proxy={use_proxy} -> {sr.status_code} bytes={len(sr.content)}")
            break


cvh_id = get_cvh_id("2280")
print("cvh_id", cvh_id)
for api_proxy in (False, True):
    url, src_ip = hls_for_route(cvh_id, api_proxy)
    print(f"\napi_proxy={api_proxy} srcIp={src_ip}")
    print("  url host", re.search(r"https://([^/]+)/", url).group(1))
    for play_proxy in (False, True):
        play_probe(url, play_proxy, None)
        play_probe(url, play_proxy, "https://animego.org/")