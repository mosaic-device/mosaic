"""Smoke-test a built/published server without a Node process."""

import contextlib
import os
import pathlib
import re
import sqlite3
import subprocess
import sys
import tempfile
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


@contextlib.contextmanager
def running_server(database):
    env = dict(os.environ, ASPNETCORE_URLS=url, Database__Path=str(database))
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
        yield
    finally:
        process.terminate()
        process.wait(timeout=5)


def check_routes():
    for path, expected, accept in [
        ("/healthz", 200, "text/plain"),
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


def query(database, sql):
    with contextlib.closing(sqlite3.connect(database)) as connection:
        return connection.execute(sql).fetchall()


def expect(actual, expected, what):
    print(f"{what}: {actual}")
    if actual != expected:
        raise AssertionError(f"Expected {what} to be {expected}, received {actual}")


with tempfile.TemporaryDirectory() as directory:
    # A missing parent directory exercises first-run creation of the configured path.
    database = pathlib.Path(directory) / "data" / "mosaic.db"

    with running_server(database):
        check_routes()
    expect(
        query(database, "SELECT name FROM sqlite_schema WHERE type = 'table' ORDER BY name"),
        [("admin",), ("schema_migrations",), ("settings",)],
        "tables after first start",
    )
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,)], "migrations after first start")

    with contextlib.closing(sqlite3.connect(database)) as connection, connection:
        connection.execute(
            "INSERT INTO admin (id, username, password_hash, created_at, updated_at)"
            " VALUES (1, 'smoke-admin', 'smoke-hash', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')"
        )
        connection.execute(
            "INSERT INTO settings (key, value, updated_at) VALUES ('smoke', 'kept', '2026-01-01T00:00:00Z')"
        )

    with running_server(database):
        check_routes()
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,)], "migrations after restart")
    expect(query(database, "SELECT username, password_hash FROM admin"), [("smoke-admin", "smoke-hash")], "admin after restart")
    expect(query(database, "SELECT value FROM settings WHERE key = 'smoke'"), [("kept",)], "settings after restart")
