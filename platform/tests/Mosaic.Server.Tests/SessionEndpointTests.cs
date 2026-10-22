using System.Net;
using System.Text.Json;

namespace Mosaic.Server.Tests;

/// <summary><c>GET /api/auth/session</c> and the shared <c>401 unauthenticated</c> against the Admin API Contract.</summary>
public sealed class SessionEndpointTests : IDisposable
{
    private readonly AuthApiFactory _factory = new();
    private readonly HttpClient _client;

    public SessionEndpointTests() => _client = _factory.CreateApiClient();

    public void Dispose()
    {
        _client.Dispose();
        _factory.Dispose();
    }

    [Fact]
    public async Task AValidSessionReturnsTheAdminAndExpiry()
    {
        var token = await SetupAsync();

        using var response = await SendAsync(AuthApiFactory.Session(token));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("application/json", response.MediaType());
        Assert.Equal("""{"admin":{"username":"Bridger"},"expiresAt":"2026-10-28T12:00:00Z"}""", await Text(response));
        Assert.False(response.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task ALoginSessionReturnsTheUsernameAsEnteredAtSetup()
    {
        await SetupAsync();
        using var login = await SendAsync(AuthApiFactory.Login("bridger"));

        using var response = await SendAsync(AuthApiFactory.Session(AuthApiFactory.SessionCookie(login).Value));

        Assert.Equal("Bridger", (await Json(response)).GetProperty("admin").GetProperty("username").GetString());
    }

    [Fact]
    public async Task ExpiresAtIsInWholeSecondsRoundedDown()
    {
        _factory.Time.Advance(TimeSpan.FromMilliseconds(999));
        var token = await SetupAsync();

        using var response = await SendAsync(AuthApiFactory.Session(token));

        Assert.Equal("2026-10-28T12:00:00Z", (await Json(response)).GetProperty("expiresAt").GetString());
    }

    [Theory]
    [InlineData("missing")]
    [InlineData("empty")]
    [InlineData("malformed")]
    [InlineData("standard Base64")]
    [InlineData("unknown")]
    [InlineData("before setup")]
    [InlineData("idle-expired")]
    [InlineData("absolute-expired")]
    [InlineData("logged out")]
    public async Task EveryInvalidSessionGetsTheSame401(string reason)
    {
        var token = reason switch
        {
            "missing" => null,
            "empty" => "",
            "malformed" => "not-a-session",
            "standard Base64" => new string('A', 42) + "+",
            "unknown" => await SetupAndReturn(new string('A', 43)),
            "before setup" => new string('A', 43),
            "idle-expired" => await IdleExpiredTokenAsync(),
            "absolute-expired" => await AbsoluteExpiredTokenAsync(),
            "logged out" => await LoggedOutTokenAsync(),
            _ => throw new ArgumentOutOfRangeException(nameof(reason)),
        };

        using var response = await SendAsync(AuthApiFactory.Session(token));

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        Assert.Equal("application/problem+json", response.MediaType());
        var body = await Json(response);
        // The same body in every case, apart from the per-request trace ID.
        Assert.Equal(["status", "code", "title", "traceId"], body.EnumerateObject().Select(property => property.Name));
        Assert.Equal(401, body.GetProperty("status").GetInt32());
        Assert.Equal("unauthenticated", body.GetProperty("code").GetString());
        Assert.Equal("Log in to continue.", body.GetProperty("title").GetString());
        Assert.False(string.IsNullOrWhiteSpace(body.GetProperty("traceId").GetString()));
        Assert.False(response.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task ExpiredSessionsAreDeleted()
    {
        var token = await IdleExpiredTokenAsync();

        using var response = await SendAsync(AuthApiFactory.Session(token));

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        Assert.Equal(0L, SessionCount());
    }

    [Fact]
    public async Task TheSessionIsRenewedAtMostOnceADay()
    {
        var token = await SetupAsync();

        _factory.Time.Advance(TimeSpan.FromHours(23));
        using (var early = await SendAsync(AuthApiFactory.Session(token)))
        {
            Assert.False(early.Headers.Contains("Set-Cookie"));
            Assert.Equal("2026-10-28T12:00:00Z", (await Json(early)).GetProperty("expiresAt").GetString());
        }

        _factory.Time.Advance(TimeSpan.FromHours(1));
        using (var renewed = await SendAsync(AuthApiFactory.Session(token)))
        {
            var (value, attributes) = AuthApiFactory.SessionCookie(renewed);
            Assert.Equal(token, value);
            Assert.Equal(["httponly", "max-age=604800", "path=/", "samesite=strict", "secure"], attributes);
            Assert.Equal("2026-10-29T12:00:00Z", (await Json(renewed)).GetProperty("expiresAt").GetString());
        }

        _factory.Time.Advance(TimeSpan.FromHours(23));
        using (var sameDay = await SendAsync(AuthApiFactory.Session(token)))
        {
            Assert.False(sameDay.Headers.Contains("Set-Cookie"));
            Assert.Equal("2026-10-29T12:00:00Z", (await Json(sameDay)).GetProperty("expiresAt").GetString());
        }

        // Renewal kept the session alive past the original seven-day idle expiry.
        _factory.Time.Advance(TimeSpan.FromDays(6));
        using var later = await SendAsync(AuthApiFactory.Session(token));
        Assert.Equal(HttpStatusCode.OK, later.StatusCode);
    }

    [Fact]
    public async Task TheRenewedCookieNeverOutlivesTheAbsoluteExpiry()
    {
        var token = await SetupAsync();
        await RenewEverySixDaysAsync(token, times: 3);

        _factory.Time.Advance(TimeSpan.FromDays(6));
        using (var capped = await SendAsync(AuthApiFactory.Session(token)))
        {
            // Day 24 of 30: six days left, not seven.
            Assert.Contains("max-age=518400", AuthApiFactory.SessionCookie(capped).Attributes);
            Assert.Equal("2026-11-20T12:00:00Z", (await Json(capped)).GetProperty("expiresAt").GetString());
        }

        _factory.Time.Advance(TimeSpan.FromDays(5));
        using (var lastDay = await SendAsync(AuthApiFactory.Session(token)))
        {
            Assert.Equal(HttpStatusCode.OK, lastDay.StatusCode);
            Assert.False(lastDay.Headers.Contains("Set-Cookie"));
        }

        _factory.Time.Advance(TimeSpan.FromDays(1));
        using var expired = await SendAsync(AuthApiFactory.Session(token));
        Assert.Equal(HttpStatusCode.Unauthorized, expired.StatusCode);
    }

    private async Task<string> SetupAsync()
    {
        using var response = await SendAsync(AuthApiFactory.Setup());
        Assert.Equal(HttpStatusCode.Created, response.StatusCode);
        return AuthApiFactory.SessionCookie(response).Value;
    }

    private async Task<string> SetupAndReturn(string token)
    {
        await SetupAsync();
        return token;
    }

    private async Task<string> IdleExpiredTokenAsync()
    {
        var token = await SetupAsync();
        _factory.Time.Advance(TimeSpan.FromDays(7));
        return token;
    }

    private async Task<string> AbsoluteExpiredTokenAsync()
    {
        var token = await SetupAsync();
        await RenewEverySixDaysAsync(token, times: 4);
        _factory.Time.Advance(TimeSpan.FromDays(6));
        return token;
    }

    private async Task<string> LoggedOutTokenAsync()
    {
        var token = await SetupAsync();
        using var response = await SendAsync(AuthApiFactory.Logout(token));
        Assert.Equal(HttpStatusCode.NoContent, response.StatusCode);
        return token;
    }

    private async Task RenewEverySixDaysAsync(string token, int times)
    {
        for (var i = 0; i < times; i++)
        {
            _factory.Time.Advance(TimeSpan.FromDays(6));
            using var response = await SendAsync(AuthApiFactory.Session(token));
            Assert.Equal(HttpStatusCode.OK, response.StatusCode);
            Assert.True(response.Headers.Contains("Set-Cookie"));
        }
    }

    private async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request)
    {
        using (request)
        {
            return await _client.SendAsync(request, TestContext.Current.CancellationToken);
        }
    }

    private long SessionCount()
    {
        using var connection = _factory.Database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT COUNT(*) FROM sessions";
        return (long)command.ExecuteScalar()!;
    }

    private static Task<string> Text(HttpResponseMessage response) =>
        response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken);

    private static async Task<JsonElement> Json(HttpResponseMessage response) =>
        JsonDocument.Parse(await Text(response)).RootElement.Clone();
}
