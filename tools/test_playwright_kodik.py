"""Проверка: работает ли Kodik в реальном Chromium (обход DDoS-Guard)."""
import json
import sys

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("playwright not installed")
    sys.exit(1)

TOKEN = "56a768d08f43091901c44b54fe970049"
import requests

r = requests.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_episodes": "true"},
    timeout=30,
)
ep_link = "https:" + r.json()["results"][0]["seasons"]["1"]["episodes"]["1"]
print("open", ep_link)

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page()
    responses = []

    def on_response(resp):
        if "/ftor" in resp.url or "m3u8" in resp.url or "hls" in resp.url:
            responses.append((resp.url, resp.status, resp.headers.get("content-type", "")))

    page.on("response", on_response)
    page.goto(ep_link, wait_until="networkidle", timeout=60000)
    page.wait_for_timeout(5000)
    # click play if present
    btn = page.query_selector(".play_button, .play-button, [class*='play']")
    if btn:
        btn.click()
        page.wait_for_timeout(8000)
    print("responses", len(responses))
    for u, st, ct in responses[:10]:
        print(st, ct, u[:120])
    browser.close()