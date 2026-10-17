# Mosaic.Server

ASP.NET Core on .NET 10, with Native AOT publishing enabled. The server exposes
`GET /healthz` (HTTP 200, plain text `OK`) and serves the built React admin app from
`wwwroot/`. Browser navigation falls back to `index.html`; missing assets and
unimplemented `/api` and `/ws` paths return 404.

Authentication, persistence, pairing, integrations, and device transport are not
implemented yet. The admin page is currently a placeholder.

Run all commands below from the **repository root**. Unless a block is labeled
otherwise, commands work unchanged in PowerShell on Windows and in a macOS or Linux
shell. This is a .NET project, not a pnpm workspace package.

## Local development

Install the .NET SDK selected by [`global.json`](../../../global.json) (currently
10.0.401, with patch roll-forward allowed). For the admin app, install Node and
pnpm as documented in the repository setup instructions; the Docker admin stage
uses Node 24.19.0 and pnpm 12.8.1.

Restore and start the backend. Windows (PowerShell):

```powershell
dotnet restore platform/Mosaic.slnx
$backendRoot = (Resolve-Path ./platform/apps/backend).Path
dotnet run --project platform/apps/backend/Mosaic.Server.csproj -- --contentRoot "$backendRoot" --urls http://localhost:5000
```

macOS/Linux:

```sh
dotnet restore platform/Mosaic.slnx
dotnet run --project platform/apps/backend/Mosaic.Server.csproj -- --contentRoot "$PWD/platform/apps/backend" --urls http://localhost:5000
```

Use an absolute content root: a relative `platform/apps/backend` can resolve under
the executable's output directory and fail with `DirectoryNotFoundException`.
Normal `dotnet run` and Visual Studio F5 use JIT compilation even though
`PublishAot` is enabled. Stop an existing debugging session if it uses port 5000.

In a second terminal, check health and start the admin development server:

```sh
curl --fail-with-body -i http://localhost:5000/healthz
pnpm install --frozen-lockfile
pnpm --filter @mosaic/admin dev
```

In Windows PowerShell 5.1, `curl` is an alias for `Invoke-WebRequest`; use
`curl.exe` there. PowerShell 7 and macOS/Linux shells run the real `curl`.

Open the Vite URL printed in that terminal. Vite proxies `/api` and `/ws` to the
backend on port 5000; those application routes are not implemented yet. Stop each
foreground server with Ctrl+C.

To serve a built admin app directly from the local backend, run this before
starting it, then open `http://localhost:5000/`:

```sh
pnpm build:admin-assets
```

This copies the admin build into `platform/apps/backend/wwwroot/`. Without those
assets, `/` may return 404 while `/healthz` still works.

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
docker run --rm --name mosaic-aot -p 127.0.0.1:5080:8080 mosaic-server:aot
```

Leave the last command running. Open `http://localhost:5080/` for the admin app, or
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
`/out/Mosaic.Server`. The script starts the server on `127.0.0.1:55881`, checks it,
and stops it. An unexpected response or startup failure fails the build.

The seven expected responses are:

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
Successful verification of this scaffold does not establish database, login,
pairing, or device behavior, or support for ARM64 and other deployment targets.

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

Build and formatting checks for the backend:

```sh
dotnet build platform/Mosaic.slnx
dotnet format platform/Mosaic.slnx --verify-no-changes
```

Review AOT/trimming warnings rather than suppressing them to obtain a passing
publish. Keep local `wwwroot`, publish outputs, and IDE files out of commits.

## Generated device protocol

`Generated/DeviceProtocol/` contains committed C# bindings generated from
`platform/packages/protocols/events.proto`. Do not edit them directly. When the
schema changes, regenerate from the repository root (requires `buf.build`):

```sh
pnpm generate-protos
```

Ordinary builds use the committed bindings and do not require regeneration or
access to `buf.build`. The bindings are not connected to a device transport yet.
