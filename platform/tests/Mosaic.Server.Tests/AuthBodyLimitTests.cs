using System.Net;
using System.Text;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests;

/// <summary>Size and time limits on setup and login request bodies.</summary>
public sealed class AuthBodyLimitTests
{
    /// <summary>A valid login body padded with an ignored field to exactly <paramref name="totalBytes"/>.</summary>
    private static string PaddedLogin(int totalBytes)
    {
        const string prefix = "{\"username\":\"Bridger\",\"password\":\"mosaic-desk-2026\",\"padding\":\"";
        const string suffix = "\"}";
        return prefix + new string('x', totalBytes - prefix.Length - suffix.Length) + suffix;
    }

    [Fact]
    public async Task BodiesOverTheLimitGetAnEmpty413()
    {
        using var factory = new AuthApiFactory();
        using var client = factory.CreateApiClient();
        Assert.Equal(HttpStatusCode.RequestEntityTooLarge,
            (await SendAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/setup", PaddedLogin(AuthEndpoints.MaxBodyBytes + 1)))).StatusCode);
        await SendAsync(client, AuthApiFactory.Setup());

        using var response = await SendAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/auth/login", PaddedLogin(AuthEndpoints.MaxBodyBytes + 1)));

        Assert.Equal(HttpStatusCode.RequestEntityTooLarge, response.StatusCode);
        Assert.Equal("", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken));
        Assert.False(response.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task BodiesAtTheLimitAreAccepted()
    {
        using var factory = new AuthApiFactory();
        using var client = factory.CreateApiClient();
        await SendAsync(client, AuthApiFactory.Setup());
        var body = PaddedLogin(AuthEndpoints.MaxBodyBytes);
        Assert.Equal(AuthEndpoints.MaxBodyBytes, Encoding.UTF8.GetByteCount(body));

        using var response = await SendAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/auth/login", body));

        Assert.True(response.StatusCode == HttpStatusCode.OK, await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task AStalledBodyHasItsConnectionClosed()
    {
        using var factory = new AuthApiFactory(settings => settings[AuthRateLimitOptions.BodyReadTimeoutSecondsKey] = "1");
        using var client = factory.CreateApiClient();
        await SendAsync(client, AuthApiFactory.Setup());

        using var stalled = new HttpRequestMessage(HttpMethod.Post, "/api/auth/login")
        {
            Content = new StalledJsonContent("""{"username":"Bridger","password":"mosaic-desk-2026"}"""),
        };
        stalled.Headers.Add("Origin", AuthApiFactory.PublicOrigin);

        // The body never finishes; the server gives up after the timeout instead of waiting forever.
        await Assert.ThrowsAnyAsync<Exception>(() =>
            client.SendAsync(stalled, TestContext.Current.CancellationToken).WaitAsync(TimeSpan.FromSeconds(15), TestContext.Current.CancellationToken));

        using var next = await SendAsync(client, AuthApiFactory.Login());
        Assert.Equal(HttpStatusCode.OK, next.StatusCode);
    }

    [Fact]
    public async Task CompletedSetupAnswers409WithoutReadingTheBody()
    {
        using var factory = new AuthApiFactory();
        using var client = factory.CreateApiClient();
        await SendAsync(client, AuthApiFactory.Setup());

        using var oversized = await SendAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/setup", PaddedLogin(AuthEndpoints.MaxBodyBytes + 1)));
        Assert.Equal(HttpStatusCode.Conflict, oversized.StatusCode);

        // A body that never arrives must not delay the answer either.
        using var stalled = new HttpRequestMessage(HttpMethod.Post, "/api/setup") { Content = new StalledJsonContent("{}") };
        stalled.Headers.Add("Origin", AuthApiFactory.PublicOrigin);
        using var response = await client.SendAsync(stalled, TestContext.Current.CancellationToken)
            .WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("setup_already_completed", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken), StringComparison.Ordinal);
    }

    [Fact]
    public async Task LoginBeforeSetupAnswers409WithoutReadingTheBody()
    {
        using var factory = new AuthApiFactory();
        using var client = factory.CreateApiClient();

        using var response = await SendAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/auth/login", PaddedLogin(AuthEndpoints.MaxBodyBytes + 1)));

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("setup_required", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken), StringComparison.Ordinal);
    }

    private static async Task<HttpResponseMessage> SendAsync(HttpClient client, HttpRequestMessage request)
    {
        using (request)
        {
            return await client.SendAsync(request, TestContext.Current.CancellationToken);
        }
    }
}
