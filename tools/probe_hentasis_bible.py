#!/usr/bin/env python3
import re, requests
UA={"User-Agent":"Mozilla/5.0"}
# crawl main page for bible
for url in ["http://hentasis1.top/", "http://v3.hentasis5.top/"]:
    r=requests.get(url,headers=UA,timeout=20)
    hits=re.findall(r'href="([^"]*bible[^"]*)"',r.text,re.I)
    hits+=re.findall(r'href="([^"]*библ[^"]*)"',r.text,re.I)
    print(url,"bible hrefs",hits[:10])
    # sitemap?
    for path in ["/sitemap.xml","/robots.txt"]:
        try:
            rr=requests.get(url.rstrip('/')+path,headers=UA,timeout=10)
            print(" ",path,rr.status_code,len(rr.text))
            if "bible" in rr.text.lower():
                print("   has bible")
        except: pass