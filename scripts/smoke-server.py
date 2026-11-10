"""Smoke-test a built/published server without a Node process."""

import base64
import contextlib
import hashlib
import json
import os
import pathlib
import re
import secrets
import socket
import struct
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
    """Checks setup, login and logout. Returns the setup session, which stays signed in."""
    setup_cookie = None
    status, body, _ = api("GET", "/api/setup")
    expect((status, body), (200, {"setupRequired": setup_required}), "GET /api/setup")
    if setup_required:
        expect_unauthenticated(None, "GET /api/auth/session before setup")
        status, body, setup_cookie = api("POST", "/api/setup", {"username": "Smoke-Admin", "password": password, "setupCode": setup_code})
        expect((status, body), (201, {"admin": {"username": "Smoke-Admin"}}), "POST /api/setup")
        expect(setup_cookie is not None, True, "setup issued a session cookie")
        expect_session(setup_cookie, "GET /api/auth/session after setup")
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
    return setup_cookie


def expect_session(cookie, what):
    status, body, _ = api("GET", "/api/auth/session", cookie=cookie)
    expect((status, body["admin"]), (200, {"username": "Smoke-Admin"}), what)
    expect(re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", body["expiresAt"]) is not None, True, f"{what} expiresAt format")


def expect_unauthenticated(cookie, what):
    status, body, cleared = api("GET", "/api/auth/session", cookie=cookie)
    expect((status, body["code"], body["title"]), (401, "unauthenticated", "Log in to continue."), what)
    # A cookie that was sent is cleared; without one, none is set.
    expect(cleared, None if cookie is None else "mosaic_session=", f"{what} Set-Cookie")


def field(number, value):
    """A protobuf length-delimited field, enough to build a device frame without a protobuf library."""
    assert len(value) < 128
    return bytes([number << 3 | 2, len(value)]) + value


def device_frame(secret):
    connect = b"\x08\x01" + field(2, secret)  # protocol_version 1, auth_secret
    return field(1, connect)


def device_handshake(frame):
    """Opens /ws/device like the firmware (no Origin), sends one binary frame and returns the reply."""
    host, port = url.removeprefix("http://").split(":")
    with socket.create_connection((host, int(port)), timeout=5) as connection:
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        connection.sendall(
            f"GET /ws/device HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode()
        )
        response = b""
        while b"\r\n\r\n" not in response:
            response += connection.recv(1)
        expect(response.split(b"\r\n")[0], b"HTTP/1.1 101 Switching Protocols", "device WebSocket upgrade")
        mask = secrets.token_bytes(4)
        connection.sendall(bytes([0x82, 0x80 | len(frame)]) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(frame)))
        opcode, length = connection.recv(1)[0] & 0x0F, connection.recv(1)[0]
        if length == 126:
            length = struct.unpack(">H", connection.recv(2))[0]
        payload = b""
        while len(payload) < length:
            payload += connection.recv(length - len(payload))
        expect(opcode, 2, "device reply is a binary frame")
        return payload


def check_device(database, secret, paired, revoked=False):
    """Connects as the device and checks its reply. Returns the code an unpaired device is shown."""
    reply = device_handshake(device_frame(secret))
    if revoked:
        # rejected with reason CREDENTIAL_REVOKED (3).
        expect(reply, field(3, b"\x08\x03"), "removed device handshake")
        return None
    if paired:
        expect(reply, field(2, b""), "paired device handshake")
        return None
    # pairing_pending: an 8-character code, then its seconds left as a two-byte varint. The
    # server rounds down, so a fresh code reports 599 or 600 depending on when it is sent.
    expect((reply[0], reply[2], reply[-3]), (0x0A, 0x0A, 0x10), "unpaired device handshake")
    seconds = (reply[-2] & 0x7F) | (reply[-1] << 7)
    expect(590 <= seconds <= 600, True, f"pairing code lifetime ({seconds} s)")
    code = reply[4:12].decode()
    expect(re.fullmatch(r"[A-HJ-NP-Z2-9]{8}", code) is not None, True, "pairing code alphabet")
    # The code is in memory only; the device table is untouched until a claim.
    expect(query(database, "SELECT COUNT(*) FROM devices"), [(0,)], "no device stored before a claim")
    return code


def claim(code, cookie):
    return api("POST", "/api/devices/claim", {"code": code}, cookie=cookie)[:2]


def check_claim(database, secret, code, cookie):
    """Claims the code the device showed, as the admin would type it, after the device went offline."""
    status, body = claim(f"{code[:4]}-{code[4:]}".lower(), cookie)
    expect(status, 200, "POST /api/devices/claim")
    device = body["device"]
    expect((sorted(device), device["id"]), (["connected", "id", "pairedAt"], 1), "claimed device")
    expect(re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", device["pairedAt"]) is not None, True, "pairedAt format")
    status, body = claim(code, cookie)
    expect((status, body["device"]["id"], body["device"]["pairedAt"]), (200, device["id"], device["pairedAt"]), "retried claim")
    status, body = claim("ABCD-EFGH", cookie)
    expect((status, body["code"]), (404, "invalid_pairing_code"), "claim with an unknown code")
    expect(claim(code, None)[0], 401, "claim without a session")
    # The server stores only the SHA-256 of the secret.
    expect(query(database, "SELECT secret_hash FROM devices"), [(hashlib.sha256(secret).digest(),)], "device stored by the claim")


def list_devices(cookie):
    status, body, _ = api("GET", "/api/devices", cookie=cookie)
    expect(status, 200, "GET /api/devices")
    return body["devices"]


def check_removal(database, cookie):
    """Lists the paired device, removes it, and checks it is gone but its row is kept as revoked."""
    devices = list_devices(cookie)
    expect([(device["id"], sorted(device)) for device in devices], [(1, ["connected", "id", "pairedAt"])], "devices before removal")
    expect(api("DELETE", "/api/devices/1", cookie=cookie)[:2], (204, None), "DELETE /api/devices/1")
    status, body, _ = api("DELETE", "/api/devices/1", cookie=cookie)
    expect((status, body["code"]), (404, "device_not_found"), "removing it again")
    expect(api("DELETE", "/api/devices/1")[0], 401, "removal without a session")
    expect(list_devices(cookie), [], "devices after removal")
    expect(query(database, "SELECT id, revoked_at IS NOT NULL FROM devices"), [(1, 1)], "removed device kept as revoked")


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
        admin = check_auth(setup_required=True, password=password)
        device_secret = secrets.token_bytes(32)
        code = check_device(database, device_secret, paired=False)
        check_claim(database, device_secret, code, admin)
        # Offline when claimed, so it learns it is paired on reconnect.
        check_device(database, device_secret, paired=True)
    expect(
        query(database, "SELECT name FROM sqlite_schema WHERE type = 'table' ORDER BY name"),
        [("admin",), ("devices",), ("schema_migrations",), ("sessions",), ("settings",)],
        "tables after first start",
    )
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,), (2,), (3,)], "migrations after first start")
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
        check_device(database, device_secret, paired=True)
        # Codes live only in memory, so the old one is forgotten, but the pairing is kept.
        status, body = claim(code, admin)
        expect((status, body["code"]), (404, "invalid_pairing_code"), "claim of a code from before the restart")
        check_removal(database, admin)
        check_device(database, device_secret, paired=False, revoked=True)
    expect(query(database, "SELECT version FROM schema_migrations"), [(1,), (2,), (3,)], "migrations after restart")
    expect(query(database, "SELECT username, password_hash FROM admin"), [("Smoke-Admin", stored_hash)], "admin after restart")
    expect(query(database, "SELECT value FROM settings WHERE key = 'smoke'"), [("kept",)], "settings after restart")

    # A removal is permanent: the old secret is still rejected after another restart.
    with running_server(database):
        check_device(database, device_secret, paired=False, revoked=True)
        expect(list_devices(admin), [], "devices after the removal and a restart")
