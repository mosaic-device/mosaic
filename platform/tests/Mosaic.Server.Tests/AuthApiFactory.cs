using System.Net;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.DependencyInjection;
using Mosaic.Server.Data;

namespace Mosaic.Server.Tests;

/// <summary>Hosts the real server against a temporary database with a manual clock and fixed setup code.</summary>
public sealed class AuthApiFactory : WebApplicationFactory<Program>
{
    public const string PublicOrigin = "https://mosaic.test";
    public const string DevOrigin = "http://localhost:5173";
    public const string Code = "K7QF-2M9X";

    /// <summary>Test-only header giving the address of the connection's peer, which the test server leaves unset.</summary>
    public const string PeerHeader = "Test-Peer-Address";

    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));
    private readonly Dictionary<string, string?> _settings;

    public AuthApiFactory(Action<Dictionary<string, string?>>? configure = null)
    {
        _settings = new()
        {
            [MosaicDatabase.PathKey] = DatabasePath,
            ["Auth:PublicOrigin"] = PublicOrigin,
            ["Auth:AdditionalOrigins:0"] = DevOrigin,
            ["Auth:SecureCookie"] = "true",
            ["MOSAIC_SETUP_CODE"] = Code,
        };
        configure?.Invoke(_settings);
    }

    public ManualTimeProvider Time { get; } = new(new DateTimeOffset(2026, 10, 21, 12, 0, 0, TimeSpan.Zero));

    public string DatabasePath => Path.Combine(_directory, "mosaic.db");

    public MosaicDatabase Database => new(DatabasePath);

    public HttpClient CreateApiClient() =>
        CreateClient(new WebApplicationFactoryClientOptions { HandleCookies = false, AllowAutoRedirect = false });

    public static HttpRequestMessage Json(HttpMethod method, string path, string json, string? origin = PublicOrigin)
    {
        var request = new HttpRequestMessage(method, path) { Content = new StringContent(json, Encoding.UTF8, "application/json") };
        if (origin is not null)
        {
            request.Headers.Add("Origin", origin);
        }

        return request;
    }

    public static HttpRequestMessage Setup(string username = "Bridger", string password = "mosaic-desk-2026", string code = Code) =>
        Json(HttpMethod.Post, "/api/setup", $$"""{"username":"{{username}}","password":"{{password}}","setupCode":"{{code}}"}""");

    public static HttpRequestMessage Login(string username = "Bridger", string password = "mosaic-desk-2026") =>
        Json(HttpMethod.Post, "/api/auth/login", $$"""{"username":"{{username}}","password":"{{password}}"}""");

    public static HttpRequestMessage Logout(string? token, string? origin = PublicOrigin)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/api/auth/logout");
        if (origin is not null)
        {
            request.Headers.Add("Origin", origin);
        }

        if (token is not null)
        {
            request.Headers.Add("Cookie", $"mosaic_session={token}");
        }

        return request;
    }

    /// <summary>The session cookie's value and lower-cased attributes from a response.</summary>
    public static (string Value, string[] Attributes) SessionCookie(HttpResponseMessage response)
    {
        var header = Assert.Single(response.Headers.GetValues("Set-Cookie"), value => value.StartsWith("mosaic_session=", StringComparison.Ordinal));
        var parts = header.Split(';', StringSplitOptions.TrimEntries);
        return (parts[0]["mosaic_session=".Length..], parts[1..].Select(part => part.ToLowerInvariant()).Order().ToArray());
    }

    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        builder.UseEnvironment("Production");
        foreach (var (key, value) in _settings)
        {
            builder.UseSetting(key, value);
        }

        builder.ConfigureServices(services =>
        {
            services.AddSingleton<TimeProvider>(Time);
            services.AddSingleton<IStartupFilter, PeerAddressFilter>();
        });
    }

    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing && Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }
}

/// <summary>Sets the connection's remote address from <see cref="AuthApiFactory.PeerHeader"/>, ahead of the server's own middleware.</summary>
internal sealed class PeerAddressFilter : IStartupFilter
{
    public Action<IApplicationBuilder> Configure(Action<IApplicationBuilder> next) => app =>
    {
        app.Use((context, nextMiddleware) =>
        {
            if (context.Request.Headers.Remove(AuthApiFactory.PeerHeader, out var peer))
            {
                context.Connection.RemoteIpAddress = IPAddress.Parse(peer.ToString());
            }

            return nextMiddleware(context);
        });
        next(app);
    };
}

/// <summary>A clock that stands still until a test moves it, so expiry can be tested without waiting.</summary>
public sealed class ManualTimeProvider(DateTimeOffset start) : TimeProvider
{
    private DateTimeOffset _now = start;

    public override DateTimeOffset GetUtcNow() => _now;

    public void Advance(TimeSpan by) => _now += by;
}

internal static class ResponseExtensions
{
    public static string? MediaType(this HttpResponseMessage response) => response.Content.Headers.ContentType?.MediaType;
}
