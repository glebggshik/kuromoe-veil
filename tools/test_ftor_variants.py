import json
import re
import requests
from base64 import b64decode

TOKEN = "56a768d08f43091901c44b54fe970049"


def extract_json(text, marker):
    start = text.find(marker)
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return json.loads(text[brace : i + 1])
    return None


def run_test(shikimori_id, translation_id, episode=1):
    s = requests.Session()
    s.headers.update(
        {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            "Accept-Language": "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7",
        }
    )

    r = s.post(
        "https://kodik-api.com/search",
        data={
            "token": TOKEN,
            "shikimori_id": shikimori_id,
            "translation_id": translation_id,
            "limit": "1",
        },
        timeout=30,
    )
    player = "https:" + r.json()["results"][0]["link"]
    s.get(player, timeout=30)
    m = re.search(r"kodikplayer\.com/(?:serial|video)/(\d+)/([a-f0-9]+)/", player)
    mid, mh = m.group(1), m.group(2)
    ep = (
        f"https://kodikplayer.com/serial/{mid}/{mh}/720p"
        f"?min_age=16&first_url=false&season=1&episode={episode}"
    )
    page = s.get(ep, headers={"Referer": player}, timeout=30).text
    params = extract_json(page, "urlParams")

    hc = ""
    for m2 in re.finditer(r"<script([^>]*)>([\s\S]*?)</script>", page):
        if ".type = '" in m2.group(2):
            hc = m2.group(2)
            break

    def q(field):
        mk = f".{field} = '"
        idx = hc.find(mk)
        if idx < 0:
            return ""
        start = idx + len(mk)
        end = hc.find("'", start)
        return hc[start:end]

    vinfo = {f: q(f) for f in ["type", "hash", "id", "link", "secret", "uid"]}
    m_app = re.search(r'src="(/assets/js/app\.(?:serial|video)\.[a-f0-9]+\.js)"', page)
    script = s.get("https://kodikplayer.com" + m_app.group(1), headers={"Referer": ep}, timeout=30).text
    ap = script.find("$.ajax")
    cp = script.find("cache:!1", ap)
    post = b64decode(script[ap + 30 : cp - 3]).decode()

    base = {
        "hash": vinfo["hash"],
        "id": vinfo["id"],
        "type": vinfo["type"],
        "d": params["d"],
        "d_sign": params["d_sign"],
        "pd": params["pd"],
        "pd_sign": params["pd_sign"],
        "ref": params.get("ref", ""),
        "ref_sign": params["ref_sign"],
    }

    variants = [
        ("bad_true_cdn_true", {**base, "bad_user": "true", "cdn_is_working": "true"}),
        ("bad_false_cdn_true", {**base, "bad_user": "false", "cdn_is_working": "true"}),
        ("bad_false_cdn_false", {**base, "bad_user": "false", "cdn_is_working": "false"}),
    ]
    if vinfo["link"]:
        variants.append(("with_link", {**variants[1][1], "link": vinfo["link"]}))

    print(f"\n=== shikimori={shikimori_id} tr={translation_id} ep={episode} ===")
    print("cookies", list(s.cookies.keys()))
    print("vinfo", vinfo)
    for label, form in variants:
        resp = s.post(
            "https://kodikplayer.com" + post,
            data=form,
            headers={"Referer": ep, "Content-Type": "application/x-www-form-urlencoded"},
            timeout=30,
        )
        body = resp.text[:120].replace("\n", " ")
        print(f"  {label}: {resp.status_code} {body}")


run_test("16498", "609", 1)  # AoT
run_test("1535", "610", 1)  # Death Note Anilibria