import sys, requests
sys.path.insert(0, r"C:\Users\eblan\Documents\anime_client")
from core import video_source

url = video_source.get_episode_stream("16498", 1, "609")
print("url", url)
p = video_source.get_parser()
proxies = p.proxies
for label, kwargs in [
    ("plain GET", {}),
    ("range", {"headers": {"Range": "bytes=0-1"}}),
]:
    r = requests.get(url, proxies=proxies, timeout=15, **kwargs)
    print(label, "status", r.status_code, "len", len(r.content))