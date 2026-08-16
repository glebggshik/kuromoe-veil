from playwright.sync_api import sync_playwright

URL = "https://kodikplayer.com/seria/102018/8fed0e8f1d366fd9d291f9d73cfc2c27/720p"

with sync_playwright() as p:
    browser = p.chromium.launch(headless=False)
    page = browser.new_page()
    events = []

    def on_request(req):
        if "kodik" in req.url or "ftor" in req.url:
            events.append(("REQ", req.method, req.url[:100]))

    def on_response(resp):
        if "kodik" in resp.url or "ftor" in resp.url:
            events.append(("RES", resp.status, resp.url[:100], len(resp.body() if resp.ok else b"")))

    page.on("request", on_request)
    page.on("response", on_response)
    page.goto(URL, wait_until="networkidle", timeout=90000)
    page.wait_for_timeout(15000)
    print("events", len(events))
    for e in events[:40]:
        print(e)
    print("title", page.title())
    print("body snippet", page.content()[:500])
    browser.close()