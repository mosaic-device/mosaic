# Mosaic.Server

ASP.NET Core on .NET 10, with Native AOT publishing enabled. The server exposes
`GET /healthz` (HTTP 200, plain text `OK`), the first-run setup, login, and session API
(see [Admin authentication](#admin-authentication)), the device WebSocket at `/ws/device`
(see [Device connections](#device-connections)), and serves the built React admin app
from `wwwroot/`. Browser navigation falls back to `index.html`; missing assets and
unimplemented `/api` and `/ws` paths return 404.

At startup the server opens its SQLite database and applies any pending schema
migrations (see [Database](#database)). Claiming and managing devices from the admin
app, notifications, and integrations are not implemented yet. Keep the server
Native-AOT-compatible: [PR checks](../../../docs/ci.md) publish it for Linux x64 with
AOT/trimming warnings as errors and run the smoke test against the result.

Run all commands below from the **repository root**. This is a .NET project, not a
pnpm workspace package; the root `pnpm server:*` scripts are shortcuts for `dotnet`.

## Local development

Install the .NET SDK and, for the admin app, Node and pnpm ([setup](../../../docs/setup.md)).
Then start the server:

```sh
pnpm server:dev
```

This is `dotnet run` on the server project. It listens on `http://localhost:5000`;
pass `--urls http://localhost:5050` to use another port. Stop it with Ctrl+C.

[`Properties/launchSettings.json`](Properties/launchSettings.json) runs it in the
Development environment, which loads `appsettings.Development.json`: the auth
origins and non-Secure cookie for `localhost`. Rider, Visual Studio and VS Code
use the same profile. `RunWorkingDirectory` in the project file starts it in this
directory, so the content root finds `appsettings*.json` and `wwwroot/` wherever
you run it from. Started any other way, the server needs
`--contentRoot <absolute path to platform/apps/server> --environment Development`;
without the Development environment it refuses to start, because
`Auth:PublicOrigin` is unset.

Check it from a second terminal (in Windows PowerShell 5.1, use `curl.exe`):

```sh
curl --fail-with-body -i http://localhost:5000/healthz
```

Then start the admin app with `pnpm admin:dev` and open the URL Vite prints; Vite
proxies `/api` and `/ws` to port 5000. To have the server serve a built admin app
itself at `http://localhost:5000/`, run `pnpm build:admin-assets` first; it copies
the build into `wwwroot/`. Without those assets, `/` returns 404 while `/healthz`
still works.

Run the server tests with `pnpm server:test`. They cover migrations and
persistence, the admin API, and the device WebSocket driven by a fake device.

## Database

The server stores the single admin and its settings in SQLite. Set the file with
`Database:Path` in appsettings or the `Database__Path` environment variable. A
relative path resolves against the content root. The default is `data/mosaic.db`,
which is `/app/data/mosaic.db` in the runtime container. The server creates the
file and its directory if they are missing. To keep data when a container is
replaced, mount a volume there, for example `-v mosaic-data:/app/data`.
Migrations live in [`Data/MosaicDatabase.cs`](Data/MosaicDatabase.cs). Each one
runs once inside a transaction and is recorded in `schema_migrations`. Append new
migrations; never edit one that has shipped. Each connection waits up to 5 seconds for
another connection's lock (`PRAGMA busy_timeout`) instead of failing with
`database is locked`.

## Admin authentication

The API follows the [Admin API Contract](https://github.com/mosaic-device/mosaic/wiki/Admin-API-Contract):
`GET`/`POST /api/setup`, `POST /api/auth/login`, `POST /api/auth/logout`, and
`GET /api/auth/session`. Sessions are opaque tokens in an `HttpOnly`,
`SameSite=Strict` `mosaic_session` cookie; the database stores only a SHA-256 hash
of each token. Passwords are stored as PBKDF2-HMAC-SHA256 hashes.

`GET /api/auth/session` returns the signed-in admin and when the session ends,
for example `{ "admin": { "username": "bridger" }, "expiresAt": "2026-10-26T18:00:00Z" }`.
A session ends after 7 days without use or 30 days after login, whichever comes
first. At most once a day, a request with a valid session moves the 7-day limit
forward (never past the 30-day one) and reissues the cookie; the cookie's `Max-Age`
always ends with the session. Protected endpoints opt in with
`.RequireAdminSession()` (see [`Auth/AdminSessionRequirement.cs`](Auth/AdminSessionRequirement.cs)),
which answers a missing, malformed, unknown, expired, or logged-out session with
the same `401 unauthenticated` and clears the cookie if one was sent. It runs as
middleware (`UseAdminSessions()`), before the endpoint reads any request body, and
marks every response from a protected endpoint `Cache-Control: no-store`. Any unexpected failure returns
`500 internal_error` with only a trace ID; the exception itself goes to the log.

| Setting | Environment variable | Purpose |
| --- | --- | --- |
| `Auth:PublicOrigin` | `Auth__PublicOrigin` | **Required.** The origin browsers use, such as `https://<tunnel-host>`. State-changing `/api` requests must send a matching `Origin` header, or get `403 forbidden_origin`. The server refuses to start without it. |
| `Auth:AdditionalOrigins` | `Auth__AdditionalOrigins__0`, ... | Extra accepted origins. Development adds the Vite server, `http://localhost:5173`. |
| `Auth:SecureCookie` | `Auth__SecureCookie` | Marks the cookie `Secure`. Default `true`; Development sets `false` for `http://localhost`. |
| `MOSAIC_SETUP_CODE` | same | Fixed first-run setup code. Without it, the server generates one on each start. |
| `Auth:RateLimits:AttemptsPerMinute` | `Auth__RateLimits__AttemptsPerMinute` | Setup and login attempts per client per minute, combined. Default `10`. |
| `Auth:RateLimits:ConcurrentPasswordChecks` | `Auth__RateLimits__ConcurrentPasswordChecks` | Password hashes running at once across all clients. Default half the CPU count, at least `1`. |
| `Auth:RateLimits:QueuedPasswordChecks` | `Auth__RateLimits__QueuedPasswordChecks` | Attempts that may wait for a hashing slot. Default `16`. |
| `Auth:RateLimits:BodyReadTimeoutSeconds` | `Auth__RateLimits__BodyReadTimeoutSeconds` | Seconds a setup, login or device claim body may take to arrive before the connection is closed. Default `10`. |
| `ForwardedHeaders:KnownProxies` | `ForwardedHeaders__KnownProxies__0`, ... | IP addresses of reverse proxies allowed to report the client address in `X-Forwarded-For`. Default none. See [Behind a proxy](#behind-a-proxy). |

Each setup or login attempt runs a deliberately slow password hash, so both limits
reply `429 too_many_requests` with a `Retry-After` header when exceeded. A hashing
slot is taken only once the body has been read, so slow uploads cannot hold one.
Setup, login and device claim bodies over 4 KB get an empty `413`. The
attempt limit is per client address.

### Behind a proxy

Behind a reverse proxy such as Tailscale Funnel, every connection comes from the
proxy, so without configuration the attempt limit is one limit shared by everyone:
a single outside client can use up the owner's attempts. List the proxy's address in
`ForwardedHeaders:KnownProxies` and the server takes the client address from the
last `X-Forwarded-For` entry on requests from that address only. Requests from any
other address keep their connection address, so clients reaching the server
directly cannot pick their own. With nothing listed (the default), `X-Forwarded-For`
is ignored; keep it that way when the server is exposed directly. Entries must be
single addresses, not ranges or host names, or the server refuses to start.

`tailscale funnel 5000` and `tailscale serve` set `X-Forwarded-For` to the client's
address, replacing any value the client sent, and connect from the machine
`tailscaled` runs on. The address to list depends on where the server runs:

| Server runs | Proxy address it sees | Setting |
| --- | --- | --- |
| On the same machine as `tailscaled` | `127.0.0.1` | `ForwardedHeaders__KnownProxies__0=127.0.0.1` |
| In Docker, published with `-p 127.0.0.1:5000:8080` | The bridge gateway, usually `172.17.0.1` | `ForwardedHeaders__KnownProxies__0=172.17.0.1` |

`docker network inspect bridge` shows the gateway; a Compose network has its own.
The startup log names the trusted proxies, or says `X-Forwarded-For` is ignored.
Don't set
`ASPNETCORE_FORWARDEDHEADERS_ENABLED`: it trusts `X-Forwarded-For` from anyone, so
the server refuses to start with it.

`appsettings.Development.json` sets the first three for local development. While no
admin exists, the server logs `No admin account exists. Setup code: K7QF-2M9X` at
startup; enter that code in the setup screen. Scripts and `curl` must send an
`Origin` header that matches, for example:

```sh
curl -i -X POST http://localhost:5000/api/auth/login -H "Origin: http://localhost:5000" -H "Content-Type: application/json" -d "{\"username\":\"admin\",\"password\":\"your-password\"}"
```

## Device connections

Devices connect to `/ws/device` as the
[Device Connection Protocol](https://github.com/mosaic-device/mosaic/wiki/Device-Connection-Protocol)
describes; the code is in [`Devices/`](Devices). Each binary WebSocket message is one
protobuf envelope. The device's first frame must be `connect`, within 10 seconds:

- A **paired** credential gets `paired`.
- An **unknown** credential gets `pairing_pending` with an 8-character code and the
  seconds it has left. Codes last ten minutes, survive brief reconnects, are replaced
  at expiry while the device stays connected, and live only in memory, so a restart
  drops them.
- A **revoked** credential, a **malformed** frame or an **unsupported version** gets
  `rejected` with the reason, then a close, before any device access.

Frames are limited to 512 bytes from the device (larger ones close with 1009) and
4,096 bytes to it. These limits are fixed by the protocol, with room above today's
messages so a field added later still reaches the decoder; a test checks that every
generated message fits.

The `devices` table stores only the SHA-256 of each device's 32-byte secret.
Secrets and pairing codes are never logged. Requests with an `Origin` header get 403,
so a web page cannot open device connections. The server pings every 15 seconds and
drops a connection that misses its pong for 15 seconds. An open connection handles a
shutdown or a signal (paired, removed, replaced) before the device's own frames, so a
device that keeps sending can't delay them. When the server closes, it keeps reading and
dropping the device's frames until the device's close arrives, for up to a second, so a
reply sent just before the close isn't lost to a reset connection. These settings can be changed
under `Devices`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `Devices:HandshakeTimeout` | `00:00:10` | Time allowed for the first `connect` |
| `Devices:KeepAliveInterval` | `00:00:15` | Ping interval |
| `Devices:KeepAliveTimeout` | `00:00:15` | Pong timeout before the connection is dropped |
| `Devices:MaxPendingPairings` | `32` | Live pairing codes at once; more unpaired devices get `SERVER_BUSY` |
| `Devices:ClaimAttemptsPerMinute` | `10` | Signed-in claim attempts per client per minute |

## Claiming a device

`POST /api/devices/claim` with `{ "code": "K7QF-2M9X" }` pairs the device showing
that code with the admin, as section 5 of the
[Admin API Contract](https://github.com/mosaic-device/mosaic/wiki/Admin-API-Contract#5-devices)
describes. It needs a session and returns
`{ "device": { "id": 3, "pairedAt": "2026-11-06T18:30:00Z", "connected": true } }`.
The code may be typed in any case, with a dash and surrounding spaces.

- The pairing is saved before the response, and the code is marked used only once it
  is saved, so a failed write leaves the code claimable. A connected device is sent
  `paired` straight away; an offline one gets it when it reconnects.
- Used and expired codes are remembered for an hour, in memory only. Claiming a used
  code again returns the same device, so a retry or two claims at once end with one
  device. An expired code gets `410 pairing_code_expired`, a used code whose device was
  removed `409 pairing_code_used`, and anything else `404 invalid_pairing_code`.
  New codes never match a live or remembered one, and only live codes count towards
  `Devices:MaxPendingPairings`.
- Requests are checked in the contract's order: Origin, session, attempts, content
  type, size, the code's format (`400 validation_failed` under `code`), then the code.
  The session check runs before the rate limiter, so requests without a session get
  `401` and never use up the admin's attempts. Claims are counted separately from
  setup and login, per client address in the same way.

[`DevicePairing`](Devices/DevicePairing.cs) claims codes and decides what a connecting
device gets under one lock, so a device that reconnects while its code is being
claimed is never handed a new code after it is paired.

## Listing and removing devices

`GET /api/devices` returns `{ "devices": [ ... ] }`: every paired device, in the same
shape as a claim returns, sorted by `id`. Removed devices and devices still showing a
code aren't listed. `connected` is read from the open connections on every request.

`DELETE /api/devices/{id}` removes a device and returns `204`, even while it is
offline. The row is marked revoked and kept, so its secret is recognized and rejected
from then on, across restarts, and its `id` is never reused. A connected device is sent
`CREDENTIAL_REVOKED` and closed. A device that reconnects during removal is still
rejected: the handshake registers its connection before reading the credential, so it
either reads the revocation or is registered in time to be told. An `id` that is
missing or already removed gets `404 device_not_found`, so of two removals at once only
one succeeds; an `id` that isn't a whole number gets the empty `404`. Both endpoints need
a session; `DELETE` checks the Origin first. Neither is rate-limited, and logging out or a
session expiring never affects a device.

## Build and run the Linux Native AOT container

Prerequisite: Docker Desktop running Linux containers, or Docker Engine on Linux.
The initial supported build target is **Linux x64** (`linux/amd64` in Docker,
`linux-x64` in .NET). On Windows and macOS, compilation happens inside Linux
containers; host Node, pnpm, Python, and .NET installations are not required for
this path. The first build needs internet access to download images and
dependencies.

On Apple silicon Macs, `--platform linux/amd64` runs the build under x64 emulation.
Expect it to be noticeably slower than on an x64 host. Keep Docker Desktop's Rosetta
option for x86_64/amd64 emulation turned on (the default in recent versions). This
path has not yet been verified on a Mac, and native ARM64 images are not a
supported target yet.

```sh
docker version
docker info --format '{{.OSType}}/{{.Architecture}}'
docker build --platform linux/amd64 --progress=plain -f Dockerfile.aot -t mosaic-server:aot .
docker run --rm --name mosaic-aot -p 127.0.0.1:5080:8080 -e Auth__PublicOrigin=http://localhost:5080 mosaic-server:aot
```

Leave the last command running. The container log shows the setup code. Open
`http://localhost:5080/` for the admin app, or
check health from a second terminal:

```sh
curl --fail-with-body -i http://localhost:5080/healthz
```

Expect HTTP 200 and `OK`. Confirm the browser loads the referenced JavaScript and
CSS successfully. Ctrl+C stops and removes the container. Alternatively:

```sh
docker stop mosaic-aot
```

The port mapping exposes the application only on the laptop's loopback address.
It is not directly reachable by an ESP32 on the LAN; network exposure and TLS for
device connections are separate work.

[`Dockerfile.aot`](../../../Dockerfile.aot) has four stages:

- `admin`: installs pinned Node/pnpm tooling and builds the admin assets.
- `build`: uses SDK 10.0.401, Clang, and zlib development files to publish the
  native executable, including the admin `wwwroot` copied from `admin`.
- `verify`: runs the published executable through the Python smoke test.
- `runtime`: packages the published files on the .NET runtime-dependencies image
  and runs `Mosaic.Server` as the non-root `app` user, listening on port 8080.

The final image contains neither Node nor the .NET SDK or separately installed
.NET runtime. `.dockerignore` excludes host build outputs and local `wwwroot`;
the container builds its own assets. Committed token outputs remain build inputs.

## Automated publish and startup verification

Run the verification target explicitly, locally or in CI:

```sh
docker build --platform linux/amd64 --progress=plain --target verify -f Dockerfile.aot -t mosaic-server:aot-verify .
```

This builds the assets and native server, then executes
[`scripts/smoke-server.py`](../../../scripts/smoke-server.py) inside Linux against
`/out/Mosaic.Server`. The script starts the server on `127.0.0.1:55881` against a
new temporary database, checks it, runs first-run setup, a rejected login, a login,
and a logout through the API, checks `GET /api/auth/session` with a valid, missing,
malformed, and logged-out session cookie, connects an unpaired device over `/ws/device`
and expects a pairing code, claims that code through `POST /api/devices/claim` once the
device has gone, retries the claim, and reconnects the device to see it paired. It
then stops the server, confirms the schema exists, the password is stored hashed and
only the device's secret hash is stored, saves a settings row, restarts the server,
and confirms the admin can still log in, the device is still authenticated as paired
while its old code is forgotten, the rows remain, and no migration ran twice. It then
lists the device, removes it through `DELETE /api/devices/1`, and checks the device is
no longer listed and its reconnect is rejected with `CREDENTIAL_REVOKED`, also after a
third start. An unexpected response, startup failure, or database mismatch fails the build.

On each start the seven expected responses are:

- `/healthz`: 200.
- `/`: 200 with an HTML navigation request.
- The JavaScript asset referenced by `index.html`: 200.
- `/client-route`: 200 with an HTML navigation request, exercising SPA fallback.
- `/assets/missing.js`, `/api/missing`, and `/ws`: 404.

`/client-route` checks server fallback only; it does not prove a corresponding
React route exists. The script checks HTTP statuses, not full browser behavior or
CSS rendering. The manual runtime-container check above complements it: `verify`
inherits the SDK build environment, while `runtime` uses a smaller base image.

Docker may reuse a successful verification layer. To execute the verification
stage again while retaining the asset and native publish caches:

```sh
docker build --platform linux/amd64 --progress=plain --no-cache-filter verify --target verify -f Dockerfile.aot -t mosaic-server:aot-verify .
```

The normal runtime build skips this independent stage. Do not deploy or run the
`mosaic-server:aot-verify` image as the application; use `mosaic-server:aot`.

The smoke test deliberately sets `ASPNETCORE_URLS` for its private test port. A log
warning that it overrides inherited `HTTP_PORTS=8080` is expected in this stage.
Successful verification does not establish pairing or device behavior, or support
for ARM64 and other deployment targets.

## Maintenance and contributor checks

To fetch current base images before rebuilding, add `--pull`:

```sh
docker build --pull --platform linux/amd64 -f Dockerfile.aot -t mosaic-server:aot .
```

If Docker Scout is available and configured, inspect the final runtime image:

```sh
docker scout cves local://mosaic-server:aot --only-severity critical,high
```

A warning on the Node build image is distinct from findings in the final runtime
image. Review findings in both; a scan is not proof that application code is secure.

Build, format and test checks for the server:

```sh
pnpm server:build
pnpm server:format:check
pnpm server:test
```

Review AOT/trimming warnings rather than suppressing them to obtain a passing
publish. Keep local `wwwroot`, publish outputs, and IDE files out of commits.

## Generated device protocol

`Generated/DeviceProtocol/` contains the committed C# bindings used by
[device connections](#device-connections). Do not hand-edit them; see [protocol generation](../../packages/protocols/README.md).
