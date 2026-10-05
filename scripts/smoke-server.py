"""Smoke-test a built/published server without a Node process."""

import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

binary = pathlib.Path(sys.argv[1]).resolve()
wwwroot = binary.parent / "wwwroot"
index = (wwwroot / "index.html").read_text()
asset = re.search(r"/assets/[^\"']+\.js", index)
if asset is None:
    raise RuntimeError("Published index.html does not reference a JavaScript asset")
url = "http://127.0.0.1:55881"
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
env = dict(os.environ, ASPNETCORE_URLS=url)
process = subprocess.Popen([str(binary)], cwd=binary.parent, env=env)
try:
    for attempt in range(30):
        try:
            opener.open(
                urllib.request.Request(url + "/", headers={"Accept": "text/html"}),
                timeout=1,
            ).close()
            break
        except urllib.error.URLError:
            if process.poll() is not None or attempt == 29:
                raise RuntimeError("Server did not start")
            time.sleep(0.2)

    for path, expected, accept in [
        ("/", 200, "text/html"),
        (asset.group(), 200, "*/*"),
        ("/client-route", 200, "text/html"),
        ("/assets/missing.js", 404, "text/html"),
        ("/api/missing", 404, "text/html"),
        ("/ws", 404, "text/html"),
    ]:
        request = urllib.request.Request(url + path, headers={"Accept": accept})
        try:
            with opener.open(request, timeout=3) as response:
                actual = response.status
        except urllib.error.HTTPError as error:
            actual = error.code
        print(f"{path}: {actual}")
        if actual != expected:
            raise AssertionError(f"Expected {expected} for {path}, received {actual}")
finally:
    process.terminate()
    process.wait(timeout=5)
