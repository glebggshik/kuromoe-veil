import re
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
s = requests.Session()
s.headers.update({"User-Agent": UA, "Accept-Language": "ru-RU,ru;q=0.9"})

tests = [
    ("16498", "609", "AoT AniDUB"),
    ("1535", None, "Death Note any"),
    ("20", None, "Naruto"),
    ("59424", None, "recent title"),
]

print("IP:", requests.get("https://api.ipify.org?format=json", timeout=10).json().get("ip"))

for sid, tr_id, label in tests:
    print(f"\n=== {label} shikimori={sid} ===")
    data = {"token": TOKEN, "shikimori_id": sid, "limit": "3", "with_episodes": "true"}
    if tr_id:
        data["translation_id"] = tr_id
    r = s.post("https://kodik-api.com/search", data=data, timeout=20)
    print("API", r.status_code)
    if r.status_code != 200:
        print(r.text[:200])
        continue
    j = r.json()
    print("total", j.get("total"))
    for item in j.get("results", [])[:2]:
        link = item.get("link", "")
        tr = item.get("translation", {})
        print("  tr", tr.get("id"), tr.get("title"), "link", link)
        full = ("https:" + link) if link.startswith("//") else link
        gr = s.get(full, timeout=20, allow_redirects=True)
        final = gr.url
        print("  GET", gr.status_code, "final", final[:90], "len", len(gr.text))
        low = gr.text.lower()
        for phrase in [
            "не существует",
            "не найден",
            "not found",
            "запрещено к просмотру",
            "недоступна",
            "error",
        ]:
            if phrase in low:
                print("    !!!", phrase)
        if item.get("seasons"):
            s1 = item["seasons"].get("1", {}).get("episodes", {})
            if s1:
                ep1 = list(s1.values())[0]
                ep_full = "https:" + ep1 if ep1.startswith("//") else ep1
                er = s.get(ep_full, timeout=20)
                print("  ep1", er.status_code, ep_full[:85], "len", len(er.text))
                if "не существует" in er.text.lower() or "not found" in er.text.lower():
                    print("    ep1: страница не существует")