using System.Net;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.DependencyInjection;
using Mosaic.Server.Auth;
using Mosaic.Server.Data;
using Mosaic.Server.Tests.Auth;

namespace Mosaic.Server.Tests;

/// <summary>
/// <see cref="AdminSessionRequirement"/> on endpoints the server doesn't have yet, such as one
/// that binds a JSON body, hosted with only the services the requirement needs.
/// </summary>
public sealed class AdminSessionRequirementTests : IAsyncDisposable
{
    private const string Code = "K7QF-2M9X";

    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));
    private WebApplication? _app;
    private int _handlerCalls;

    public async ValueTask DisposeAsync()
    {
        if (_app is not null)
        {
            await _app.DisposeAsync();
        }

        SqliteConnection.ClearAllPools();
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    [Theory]
    [InlineData("""{"value":42}""")]
    [InlineData("""{"value":""")]
    [InlineData("not json")]
    public async Task AnonymousRequestsGetThe401BeforeTheBodyIsBound(string json)
    {
        var client = await StartAsync();

        using var response = await client.SendAsync(Post(json, token: null), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        Assert.Contains("\"code\":\"unauthenticated\"", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken), StringComparison.Ordinal);
        Assert.Equal(0, _handlerCalls);
    }

    [Fact]
    public async Task AStaleCookieGetsThe401BeforeTheBodyIsBound()
    {
        var client = await StartAsync();

        using var response = await client.SendAsync(Post("not json", token: new string('A', 43)), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        Assert.Contains("max-age=0", AuthApiFactory.SessionCookie(response).Attributes);
        Assert.Equal(0, _handlerCalls);
    }

    [Fact]
    public async Task AValidSessionReachesTheHandlerWithTheAdmin()
    {
        var client = await StartAsync();
        var token = await SetupAsync();

        using var response = await client.SendAsync(Post("""{"value":42}""", token), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("Bridger:42", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task AValidSessionStillGetsBindingErrors()
    {
        var client = await StartAsync();
        var token = await SetupAsync();

        using var response = await client.SendAsync(Post("not json", token), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Equal(0, _handlerCalls);
    }

    [Fact]
    public async Task EndpointsWithoutTheRequirementAreUnaffected()
    {
        var client = await StartAsync();

        using var response = await client.GetAsync("/open", TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.False(response.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task AMissingMiddlewareFailsClosed()
    {
        var client = await StartAsync(useAdminSessions: false);

        var error = await Assert.ThrowsAnyAsync<InvalidOperationException>(() =>
            client.SendAsync(Post("""{"value":42}""", token: null), TestContext.Current.CancellationToken));

        Assert.Contains(nameof(AdminSessionRequirement.UseAdminSessions), error.Message, StringComparison.Ordinal);
        Assert.Equal(0, _handlerCalls);
    }

    private async Task<HttpClient> StartAsync(bool useAdminSessions = true)
    {
        var database = new MosaicDatabase(Path.Combine(_directory, "mosaic.db"));
        database.Migrate();

        var builder = WebApplication.CreateSlimBuilder();
        builder.WebHost.UseTestServer();
        builder.Services.AddSingleton(database);
        builder.Services.AddSingleton(TimeProvider.System);
        builder.Services.AddSingleton(TestConfiguration.Options());
        builder.Services.AddSingleton(SetupCode.FromConfiguration(TestConfiguration.From((SetupCode.OverrideKey, Code))));
        builder.Services.AddSingleton(new PasswordHasher(PasswordHasher.MinIterations));
        builder.Services.AddSingleton(new PasswordCheckLimiter(AuthRateLimitOptions.FromConfiguration(TestConfiguration.From())));
        builder.Services.AddSingleton<AdminStore>();
        builder.Services.AddSingleton<SessionStore>();
        builder.Services.AddSingleton<SessionCookie>();
        builder.Services.AddSingleton<AuthService>();

        _app = builder.Build();
        if (useAdminSessions)
        {
            _app.UseAdminSessions();
        }

        _app.MapPost("/typed", (Probe probe, HttpContext context) =>
        {
            _handlerCalls++;
            return Results.Text($"{context.GetAdminSession().Admin.Username}:{probe.Value}");
        }).RequireAdminSession();
        _app.MapGet("/open", () => "open");

        await _app.StartAsync(TestContext.Current.CancellationToken);
        return _app.GetTestClient();
    }

    private async Task<string> SetupAsync()
    {
        var result = await _app!.Services.GetRequiredService<AuthService>()
            .SetupAsync(new SetupRequest("Bridger", "mosaic-desk-2026", Code), TestContext.Current.CancellationToken);
        return result.Session!.Token;
    }

    private static HttpRequestMessage Post(string json, string? token)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/typed") { Content = new StringContent(json, Encoding.UTF8, "application/json") };
        if (token is not null)
        {
            request.Headers.Add("Cookie", $"mosaic_session={token}");
        }

        return request;
    }

    private sealed record Probe(int Value);
}
