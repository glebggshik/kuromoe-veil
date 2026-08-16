"""Проверка: бан IP / геоблок / DDoS-Guard vs рабочий Kodik."""
import json
import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

print("=== 1. Внешний IP ===")
try:
    ip = requests.get("https://api.ipify.org?format=json", timeout=10).json()["ip"]
    print("IP:", ip)
except Exception as e:
    ip = "?"
    print("IP: не удалось", e)

print("\n=== 2. kodik-api.com (без прокси) ===")
s = requests.Session()
s.headers["User-Agent"] = UA
try:
    r = s.post(
        "https://kodik-api.com/search",
        data={"token": TOKEN, "shikimori_id": "16498", "limit": "1"},
        timeout=20,
    )
    print("search HTTP", r.status_code, "time", r.elapsed.total_seconds())
    if r.status_code == 200:
        j = r.json()
        print("total", j.get("total"), "link", (j.get("results") or [{}])[0].get("link", "")[:80])
except Exception as e:
    print("search FAIL", e)

print("\n=== 3. kodikplayer.com GET (страница серии) ===")
ep = "https://kodikplayer.com/seria/102018/8fed0e8f1d366fd9d291f9d73cfc2c27/720p"
try:
    r = s.get(ep, timeout=20)
    print("GET HTTP", r.status_code, "len", len(r.text), "server", r.headers.get("Server"))
    markers = [
        "запрещено к просмотру",
        "заблокирован",
        "blocked",
        "bad_user",
        "Видео запрещено",
        "недоступна",
        "/s/m/",
        "ddg",
    ]
    for m in markers:
        if m.lower() in r.text.lower():
            print("  marker found:", m)
    if r.status_code == 200 and len(r.text) > 5000:
        print("  страница OK — HTML плеера загружается")
    cookies = s.cookies.get_dict()
    print("  cookies:", list(cookies.keys()))
except Exception as e:
    print("GET FAIL", e)

print("\n=== 4. POST /ftor (признак бана vs DDoS-Guard) ===")
page = s.get(ep, timeout=20).text if "page" not in dir() else r.text

def var(name):
    m = re.search(rf'var {name}\s*=\s*"([^"]*)"', page)
    return m.group(1) if m else ""

hc = ""
for m in re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page):
    if ".type = '" in m.group(2):
        hc = m.group(2)
        break

def q(field):
    mk = f".{field} = '"
    idx = hc.find(mk)
    if idx < 0:
        return ""
    start = idx + len(mk)
    end = hc.find("'", start)
    return hc[start:end]

form = {
    "hash": q("hash"),
    "id": q("id"),
    "type": q("type"),
    "d": var("domain"),
    "d_sign": var("d_sign"),
    "pd": var("pd"),
    "pd_sign": var("pd_sign"),
    "ref": var("ref"),
    "ref_sign": var("ref_sign"),
    "bad_user": "false",
    "cdn_is_working": "true",
}
try:
    pr = s.post(
        "https://kodikplayer.com/ftor",
        data=form,
        headers={"Referer": ep, "Origin": "https://kodikplayer.com", "X-Requested-With": "XMLHttpRequest"},
        timeout=20,
    )
    print("POST HTTP", pr.status_code, "body len", len(pr.text), "server", pr.headers.get("Server"))
    if pr.status_code == 500 and len(pr.text) == 0:
        print("  → пустой 500 от DDoS-Guard: блокирует POST, не обязательно «бан аккаунта»")
    elif pr.status_code == 403:
        print("  → 403: похоже на бан IP")
    elif pr.status_code == 200:
        j = pr.json()
        src = j.get("links", {}).get("720", [{}])[0].get("src", "")[:100]
        if "/s/m/" in src:
            print("  → 200, но прокси-ссылка /s/m/ = IP в списке Kodik")
        else:
            print("  → 200 OK, stream:", src)
    else:
        print("  body:", pr.text[:200])
except Exception as e:
    print("POST FAIL", e)

print("\n=== 5. get-player embed (токен) ===")
try:
    r = s.get(
        f"https://kodik-api.com/get-player?token={TOKEN}&shikimoriID=16498&title=Player&hasPlayer=false",
        timeout=20,
    )
    print("get-player HTTP", r.status_code)
    if r.status_code == 200:
        j = r.json()
        print("found", j.get("found"), "allowed", j.get("allowed"), "link", str(j.get("link", ""))[:60])
except Exception as e:
    print("get-player FAIL", e)

print("\n=== ИТОГ ===")
print("Бан IP у Kodik обычно: GET 403, или POST 200 с ссылкой /s/m/..., или «запрещено в стране».")
print("Пустой POST 500 + Server: ddos-guard при рабочем GET = антибот, не полный бан.")