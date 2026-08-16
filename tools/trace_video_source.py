import sys
import requests

sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import video_source

# reset singleton
video_source._parser = None

orig_get = requests.get
orig_post = requests.post

def logged_get(url, *a, **kw):
    s = str(url)
    if "kodikplayer.com" in s:
        print("GET", s[:120])
        r = orig_get(url, *a, **kw)
        print("  ->", r.status_code, len(r.text))
        return r
    return orig_get(url, *a, **kw)

def logged_post(url, *a, **kw):
    s = str(url)
    if "kodikplayer.com" in s:
        print("POST", s[:120])
        r = orig_post(url, *a, **kw)
        print("  ->", r.status_code, r.text[:80])
        return r
    return orig_post(url, *a, **kw)

requests.get = logged_get
requests.post = logged_post

url = video_source.get_episode_stream("16498", 1, "609")
print("FINAL", url[:100])