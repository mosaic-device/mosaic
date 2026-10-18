# Mosaic.Server

.NET 10 ASP.NET Core server serving the admin app from `wwwroot/`. Browser
navigation falls back to `index.html`; missing `/assets`, `/api`, and `/ws`
requests return 404. No API, WebSocket, authentication, persistence, or device
transport is implemented. Keep the server Native-AOT-compatible.

From the repository root, run `dotnet restore platform/Mosaic.slnx`, then start the server with:

```sh
dotnet run --project platform/apps/backend/Mosaic.Server.csproj -- --contentRoot platform/apps/backend --urls http://localhost:5000
```

The admin dev server proxies `/api` and `/ws` to port 5000. Use `pnpm server:build`
(or `dotnet build platform/Mosaic.slnx`) and `pnpm server:format:check` for build
and formatting checks. This is a .NET project, not a pnpm workspace package.

To publish with the admin app, build and copy its assets **before** publishing:

```sh
pnpm build:admin-assets
dotnet publish platform/apps/backend/Mosaic.Server.csproj -c Release -o publish/local
python3 scripts/smoke-server.py publish/local/Mosaic.Server
```

`Generated/DeviceProtocol/` contains committed C# bindings, not yet connected to a
device transport. Do not hand-edit them; see [protocol generation](../../packages/protocols/README.md).
