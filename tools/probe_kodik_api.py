import json
import requests

TOKEN = "56a768d08f43091901c44b54fe970049"
s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0"

for endpoint, payload in [
    ("search", {"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_episodes": "true"}),
    ("search", {"token": TOKEN, "shikimori_id": "16498", "translation_id": "609", "limit": "1", "with_material_data": "true"}),
    ("get-player", {"token": TOKEN, "shikimoriID": "16498", "title": "Player", "hasPlayer": "false"}),
]:
    r = s.post(f"https://kodik-api.com/{endpoint}", data=payload, timeout=30)
    print(f"\n=== {endpoint} ===", r.status_code)
    try:
        obj = r.json()
        print(json.dumps(obj, ensure_ascii=False, indent=2)[:3000])
    except Exception as e:
        print(r.text[:500], e)