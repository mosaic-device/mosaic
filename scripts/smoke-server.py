"""Smoke-test a built/published server without a Node process."""

import contextlib
import json
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
setup_code = "SMOK-E123"
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


@contextlib.contextmanager
def running_server(database):
    env = dict(
        os.environ,
        ASPNETCORE_URLS=url,
        Database__Path=str(database),
        Auth__PublicOrigin=url,
        MOSAIC_SETUP_CODE=setup_code,
    )
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


def api(method, path, body=None, cookie=None):
    """Calls the admin API as the admin app would: JSON body, same-origin Origin header, session cookie."""
    headers = {"Origin": url}
    if cookie is not None:
        headers["Cookie"] = cookie
    data = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode()
    request = urllib.request.Request(url + path, data=data, headers=headers, method=method)
    try:
        with opener.open(request, timeout=10) as response:
            status, payload, cookies = response.status, response.read(), response.headers.get_all("Set-Cookie") or []
    except urllib.error.HTTPError as error:
        status, payload, cookies = error.code, error.read(), error.headers.get_all("Set-Cookie") or []
    session = [c.split(";")[0] for c in cookies if c.startswith("mosaic_session=")]
    return status, json.loads(payload) if payload else None, session[0] if session else None


def check_auth(setup_required, password):
    status, body, _ = api("GET", "/api/setup")
    expect((status, body), (200, {"setupRequired": setup_required}), "GET /api/setup")
    if setup_required:
        expect_unauthenticated(None, "GET /api/auth/session before setup")
        status, body, cookie = api("POST", "/api/setup", {"username": "Smoke-Admin", "password": password, "setupCode": setup_code})
        expect((status, body), (201, {"admin": {"username": "Smoke-Admin"}}), "POST /api/setup")
        expect(cookie is not None, True, "setup issued a session cookie")
        expect_session(cookie, "GET /api/auth/session after setup")
    status, body, _ = api("POST", "/api/auth/login", {"username": "smoke-admin", "password": "wrong-password-1"})
    expect((status, body["code"]), (401, "invalid_credentials"), "login with a wrong password")
    status, body, cookie = api("POST", "/api/auth/login", {"username": "smoke-admin", "password": password})
    expect((status, body), (200, {"admin": {"username": "Smoke-Admin"}}), "login")
    expect_session(cookie, "GET /api/auth/session after login")
    expect_unauthenticated(None, "GET /api/auth/session without a cookie")
    expect_unauthenticated("mosaic_session=not-a-session", "GET /api/auth/session with a malformed cookie")
    request = urllib.request.Request(
        url + "/api/auth/logout", headers={"Origin": url, "Cookie": cookie}, method="POST"
    )
    with opener.open(request, timeout=3) as response:
        cleared = [c for c in response.headers.get_all("Set-Cookie") or [] if c.startswith("mosaic_session=;")]
        expect((response.status, len(cleared)), (204, 1), "logout")
    expect_unauthenticated(cookie, "GET /api/auth/session after logout")


def expect_session(cookie, what):
    status, body, _ = api("GET", "/api/auth/session", cookie=cookie)
    expect((status, body["admin"]), (200, {"username": "Smoke-Admin"}), what)
    expect(re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", body["expiresAt"]) is not None, True, f"{what} expiresAt format")


def expect_unauthenticated(cookie, what):
    status, body, _ = api("GET", "/api/auth/session", cookie=cookie)
    expect((status, body["code"], body["title"]), (401, "unauthenticated", "Log in to continue."), what)


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

    password = "smoke-password-1"
    with running_server(database):
        check_routes()
        check_auth(setup_required=True, password=password)
    expect(
        query(database, "SELECT name FROM sqlite_schema WHERE type = 'table' ORDER BY name"),
        [("admin",), ("schema_migrations",), ("sessions",), ("settings",)],
        "tables after first start",
    )
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,), (2,)], "migrations after first start")
    # Setup and login each issued a session; logout deleted the login one.
    expect(query(database, "SELECT COUNT(*) FROM sessions"), [(1,)], "sessions after logout")
    stored_hash = query(database, "SELECT password_hash FROM admin")[0][0]
    expect(stored_hash.startswith("pbkdf2-sha256$"), True, "password stored as a PBKDF2 hash")

    with contextlib.closing(sqlite3.connect(database)) as connection, connection:
        connection.execute(
            "INSERT INTO settings (key, value, updated_at) VALUES ('smoke', 'kept', '2026-01-01T00:00:00Z')"
        )

    with running_server(database):
        check_routes()
        check_auth(setup_required=False, password=password)
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,), (2,)], "migrations after restart")
    expect(query(database, "SELECT username, password_hash FROM admin"), [("Smoke-Admin", stored_hash)], "admin after restart")
    expect(query(database, "SELECT value FROM settings WHERE key = 'smoke'"), [("kept",)], "settings after restart")
