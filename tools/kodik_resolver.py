#!/usr/bin/env python3
"""Локальный sidecar для Kodik: обход DDoS-Guard через headless Chromium.

Запуск:
  pip install playwright
  python -m playwright install chromium
  python tools/kodik_resolver.py

Клиент (C++): Настройки → Kodik resolver → http://127.0.0.1:8765

API:
  GET /health  → {"ok": true}
  GET /parse?url=https://kodikplayer.com/seria/...  → {"url": "...", "error": ""}

Прокси для Chromium (опционально):
  set KODIK_PROXY=socks5://user:pass@host:port
"""
from __future__ import annotations

import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

HOST = os.environ.get("KODIK_RESOLVER_HOST", "127.0.0.1")
PORT = int(os.environ.get("KODIK_RESOLVER_PORT", "8765"))
PROXY = os.environ.get("KODIK_PROXY", "").strip() or None

_browser = None
_playwright = None
_lock = threading.Lock()


def _ensure_browser():
    global _browser, _playwright
    if _browser is not None:
        return _browser
    from playwright.sync_api import sync_playwright

    _playwright = sync_playwright().start()
    launch_kwargs = {"headless": True}
    if PROXY:
        launch_kwargs["proxy"] = {"server": PROXY}
    _browser = _playwright.chromium.launch(**launch_kwargs)
    return _browser


def _decode_src(src: str) -> str:
    import base64
    import re

    if "mp4:hls:manifest" in src or src.startswith("http"):
        return src
    try:
        decoded = base64.b64decode(
            src.encode(),
            altchars=b"-_",
        ).decode("utf-8", errors="ignore")
        if "mp4:hls:manifest" in decoded:
            return decoded
    except Exception:
        pass
    rot = str.maketrans(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        "QRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMnopqrstuvwxyzabcdefghijklm",
    )
    for shift in range(26):
        attempt = src.translate(rot)
        pad = (4 - len(attempt) % 4) % 4
        attempt += "=" * pad
        try:
            decoded = base64.b64decode(attempt).decode("utf-8", errors="ignore")
            if "mp4:hls:manifest" in decoded:
                return decoded
        except Exception:
            continue
    return src


EXTRACT_FORM_JS = """
() => {
  const html = document.documentElement.innerHTML;
  const g = (name) => {
    const re = new RegExp('(?:var\\\\s+)?' + name + '\\\\s*=\\\\s*"([^"]*)"');
    const m = html.match(re);
    return m ? m[1] : '';
  };
  let hc = '';
  for (const s of document.querySelectorAll('script')) {
    const t = s.textContent || '';
    if (t.includes(".type = '")) { hc = t; break; }
  }
  const pick = (field) => {
    const mk = `.${field} = '`;
    const i = hc.indexOf(mk);
    if (i < 0) return '';
    const start = i + mk.length;
    return hc.substring(start, hc.indexOf("'", start));
  };
  return {
    hash: pick('hash'), id: pick('id'), type: pick('type'),
    d: g('domain'), d_sign: g('d_sign'), pd: g('pd'), pd_sign: g('pd_sign'),
    ref: g('ref'), ref_sign: g('ref_sign'),
    bad_user: 'false', cdn_is_working: 'true',
  };
}
"""


def _links_from_ftor_json(data: dict) -> str:
    links = data.get("links") or {}
    if not links:
        return ""
    mq = max(int(k) for k in links)
    src = links[str(mq)][0].get("src", "")
    return _decode_src(src) if src else ""


def resolve_stream(page_url: str) -> dict:
    result = {"url": "", "error": ""}
    with _lock:
        browser = _ensure_browser()
        context = browser.new_context(
            user_agent=(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            ),
            locale="ru-RU",
        )
        page = context.new_page()
        captured: dict = {}
        try:
            page.goto(page_url, wait_until="domcontentloaded", timeout=60000)
            page.wait_for_timeout(2500)
            form = page.evaluate(EXTRACT_FORM_JS)
            if not form.get("hash") or not form.get("d_sign"):
                result["error"] = "не удалось извлечь подписи со страницы"
                return result

            # POST через request-контекст Playwright — те же cookies/TLS, что у Chromium.
            resp = page.request.post(
                "https://kodikplayer.com/ftor",
                form=form,
                headers={
                    "Referer": page_url,
                    "Origin": "https://kodikplayer.com",
                    "X-Requested-With": "XMLHttpRequest",
                },
            )
            captured["ftor_status"] = resp.status
            if resp.status == 200:
                src = _links_from_ftor_json(resp.json())
                if src:
                    captured["url"] = src
            elif resp.status == 500 and not resp.body():
                result["error"] = "/ftor HTTP 500 (DDoS-Guard) — попробуй KODIK_PROXY=residential"
            else:
                result["error"] = f"/ftor HTTP {resp.status}"
        except Exception as ex:
            result["error"] = f"resolve: {ex}"
        finally:
            context.close()

    if captured.get("url"):
        url = captured["url"]
        if url.startswith("//"):
            url = "https:" + url
        result["url"] = url
    elif not result["error"]:
        st = captured.get("ftor_status")
        result["error"] = f"/ftor HTTP {st}" if st else "не удалось получить ссылку"
    return result


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[kodik_resolver] " + (fmt % args) + "\n")

    def _json(self, code: int, payload: dict):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path)
        if path.path in ("/health", "/"):
            self._json(200, {"ok": True, "proxy": bool(PROXY)})
            return
        if path.path == "/parse":
            qs = parse_qs(path.query)
            page_url = (qs.get("url") or [""])[0].strip()
            if not page_url:
                self._json(400, {"url": "", "error": "missing url query param"})
                return
            self._json(200, resolve_stream(page_url))
            return
        self._json(404, {"url": "", "error": "not found"})


def main():
    server = HTTPServer((HOST, PORT), Handler)
    print(f"Kodik resolver on http://{HOST}:{PORT}  proxy={PROXY or 'off'}")
    print("GET /parse?url=<kodikplayer page>")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutdown")
        global _browser, _playwright
        if _browser:
            _browser.close()
        if _playwright:
            _playwright.stop()


if __name__ == "__main__":
    main()