import re
import requests

s = requests.Session()
s.headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120"
ep = "https://kodikplayer.com/seria/102018/8fed0e8f1d366fd9d291f9d73cfc2c27/720p"
t = s.get(ep, timeout=20).text

print("status page, len", len(t))
print("title:", re.search(r"<title>([^<]+)", t, re.I).group(1) if re.search(r"<title>", t) else "?")

for pat in [
    r"не существует[^<]{0,100}",
    r"запрещено[^<]{0,100}",
    r"недоступн[^<]{0,100}",
    r"promo-error[^>]*>[\s\S]{0,300}",
    r"error-box[\s\S]{0,400}",
]:
    m = re.search(pat, t, re.I)
    if m:
        print("---")
        print(m.group(0)[:300])

i = t.lower().find("не существует")
if i >= 0:
    print("--- context ---")
    print(t[max(0, i - 200) : i + 250].replace("\n", " "))