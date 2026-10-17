using Microsoft.Extensions.FileProviders;

var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

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

app.MapGet("/healthz", async context =>
{
    context.Response.ContentType = "text/plain";
    await context.Response.WriteAsync("OK");
});

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
