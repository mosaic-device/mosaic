# Mosaic.Server

Minimal .NET 10 ASP.NET Core server. It serves the admin app from `wwwroot/` and falls back to `index.html` for browser navigation; missing assets and `/api` and `/ws` requests return 404. No API, WebSocket, authentication, persistence, or device transport is implemented yet.

From the repository root, run `dotnet restore platform/Mosaic.slnx`, then start the server with:

```sh
dotnet run --project platform/apps/backend/Mosaic.Server.csproj -- --contentRoot platform/apps/backend --urls http://localhost:5000
```

The admin Vite dev server proxies `/api` and `/ws` to port 5000. To build the server, run `pnpm server:build` (or `dotnet build platform/Mosaic.slnx`); `pnpm server:format:check` checks .NET formatting. This is a .NET project, not a pnpm workspace package.

To publish with the admin app, build and copy its assets **before** publishing:

```sh
pnpm build:admin-assets
dotnet publish platform/apps/backend/Mosaic.Server.csproj -c Release -o publish/local
python3 scripts/smoke-server.py publish/local/Mosaic.Server
```

`Generated/DeviceProtocol/` contains committed C# bindings generated from `platform/packages/protocols/events.proto`. Do not edit them directly; use `pnpm generate-protos` from the root when the schema changes (requires `buf.build`). The bindings are not connected to a device transport yet.
