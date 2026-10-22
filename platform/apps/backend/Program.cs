using Microsoft.Extensions.FileProviders;
using Mosaic.Server.Auth;
using Mosaic.Server.Data;

var builder = WebApplication.CreateBuilder(args);
// Factories read configuration after Build, so every configuration source (including test hosts) applies.
builder.Services.AddSingleton(services => MosaicDatabase.FromConfiguration(
    services.GetRequiredService<IConfiguration>(),
    services.GetRequiredService<IHostEnvironment>().ContentRootPath));
builder.Services.AddSingleton(services => AuthOptions.FromConfiguration(services.GetRequiredService<IConfiguration>()));
builder.Services.AddSingleton(services => SetupCode.FromConfiguration(services.GetRequiredService<IConfiguration>()));
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton(new PasswordHasher());
builder.Services.AddSingleton<AdminStore>();
builder.Services.AddSingleton<SessionStore>();
builder.Services.AddSingleton<SessionCookie>();
var app = builder.Build();

// Resolve configuration-backed services now so a misconfiguration fails at startup, not on first request.
var database = app.Services.GetRequiredService<MosaicDatabase>();
var authOptions = app.Services.GetRequiredService<AuthOptions>();
var appliedMigrations = database.Migrate();
app.Logger.LogInformation(
    "Database {Path} at schema version {Version}; applied {Applied} migration(s) at startup",
    database.Path,
    MosaicDatabase.LatestVersion,
    appliedMigrations.Count);
app.Logger.LogInformation("Admin API accepts requests from {Origins}", string.Join(", ", authOptions.AllowedOrigins));

if (!app.Services.GetRequiredService<AdminStore>().Exists())
{
    var setupCode = app.Services.GetRequiredService<SetupCode>();
    if (setupCode.FromOverride)
    {
        app.Logger.LogWarning("No admin account exists. Setup code: the value of {Variable}", SetupCode.OverrideKey);
    }
    else
    {
        app.Logger.LogWarning("No admin account exists. Setup code: {SetupCode}", setupCode.Display);
    }
}

var publishedWebRoot = Path.Combine(AppContext.BaseDirectory, "wwwroot");
var webRoot = Directory.Exists(publishedWebRoot)
    ? publishedWebRoot
    : Path.Combine(app.Environment.ContentRootPath, "wwwroot");
if (Directory.Exists(webRoot))
{
    var files = new PhysicalFileProvider(webRoot);
    app.UseDefaultFiles(new DefaultFilesOptions { FileProvider = files });
    app.UseStaticFiles(new StaticFileOptions { FileProvider = files });
}

app.UseOriginCheck();

app.MapGet("/healthz", async context =>
{
    context.Response.ContentType = "text/plain";
    await context.Response.WriteAsync("OK");
});

app.MapAuthEndpoints();

// SPA routes are navigation requests; API, WebSocket, and asset misses must remain 404s.
app.MapFallback(async context =>
{
    var path = context.Request.Path;
    var isNavigation = (HttpMethods.IsGet(context.Request.Method) || HttpMethods.IsHead(context.Request.Method))
        && context.Request.Headers.Accept.Any(value => value?.Contains("text/html", StringComparison.OrdinalIgnoreCase) == true);

    if (!isNavigation || path.StartsWithSegments("/api") || path.StartsWithSegments("/ws")
        || path.StartsWithSegments("/assets"))
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        return;
    }

    var indexPath = Path.Combine(webRoot, "index.html");
    if (!File.Exists(indexPath))
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        return;
    }

    context.Response.ContentType = "text/html";
    await context.Response.SendFileAsync(indexPath);
});

app.Run();
