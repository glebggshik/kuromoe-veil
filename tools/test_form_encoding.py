"""Сравнение сериализации form-urlencoded: Qt (C++) vs requests."""
import re
import requests
from urllib.parse import quote, urlencode

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0"
r = s.post(
    "https://kodik-api.com/search",
    data={"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_episodes": "true"},
    timeout=30,
)
ep_link = "https:" + r.json()["results"][0]["seasons"]["1"]["episodes"]["1"]
page = s.get(ep_link, timeout=30).text


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
    start = idx + len(mk)
    end = hc.find("'", start)
    return hc[start:end]


vinfo = {f: q(f) for f in ["type", "hash", "id"]}
form = {
    "hash": vinfo["hash"],
    "id": vinfo["id"],
    "type": vinfo["type"],
    "d": var("domain"),
    "d_sign": var("d_sign"),
    "pd": var("pd"),
    "pd_sign": var("pd_sign"),
    "ref": var("ref"),
    "ref_sign": var("ref_sign"),
    "bad_user": "false",
    "cdn_is_working": "true",
}

# requests (как C++ QUrlQuery::FullyEncoded — percent-encoding спецсимволов)
req_body = urlencode(form, quote_via=quote)
print("requests body sample:")
print(req_body[:200])
print("...")
print("d_sign in body:", form["d_sign"] in req_body or quote(form["d_sign"], safe="") in req_body)

# Проверка: двоеточие в подписи не должно ломаться
assert ":" in form["d_sign"]
assert "%3A" in req_body or form["d_sign"] in req_body
print("encoding OK — d_sign содержит :, body корректен")

resp = s.post(
    "https://kodikplayer.com/ftor",
    data=form,
    headers={"Referer": ep_link, "Content-Type": "application/x-www-form-urlencoded"},
    timeout=30,
)
print("POST status", resp.status_code, "len", len(resp.text), "server", resp.headers.get("Server"))