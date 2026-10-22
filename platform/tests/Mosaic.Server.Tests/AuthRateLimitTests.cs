using System.Net;
using System.Text.Json;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests;

/// <summary>Attempt and hashing limits on the endpoints that check passwords.</summary>
public sealed class AuthRateLimitTests
{
    [Fact]
    public async Task AttemptsBeyondTheLimitGet429WithRetryAfter()
    {
        using var factory = new AuthApiFactory(settings => settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "3");
        using var client = factory.CreateApiClient();

        // Setup and login share one window per client.
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup()));
        Assert.Equal(HttpStatusCode.Unauthorized, await StatusAsync(client, AuthApiFactory.Login(password: "wrong-password-1")));
        Assert.Equal(HttpStatusCode.Unauthorized, await StatusAsync(client, AuthApiFactory.Login(password: "wrong-password-2")));

        using var limited = await client.SendAsync(AuthApiFactory.Login(), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.TooManyRequests, limited.StatusCode);
        Assert.Equal("application/problem+json", limited.MediaType());
        var body = JsonDocument.Parse(await limited.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement;
        Assert.Equal("too_many_requests", body.GetProperty("code").GetString());
        Assert.Equal(429, body.GetProperty("status").GetInt32());
        var retryAfter = int.Parse(Assert.Single(limited.Headers.GetValues("Retry-After")), System.Globalization.CultureInfo.InvariantCulture);
        Assert.InRange(retryAfter, 1, 60);
        Assert.False(limited.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task OtherEndpointsAreNotLimited()
    {
        using var factory = new AuthApiFactory(settings => settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "1");
        using var client = factory.CreateApiClient();
        await StatusAsync(client, AuthApiFactory.Setup());
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, AuthApiFactory.Login()));

        for (var i = 0; i < 3; i++)
        {
            Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/api/setup", TestContext.Current.CancellationToken)).StatusCode);
            Assert.Equal(HttpStatusCode.NoContent, await StatusAsync(client, AuthApiFactory.Logout(null)));
        }
    }

    [Fact]
    public async Task RejectedOriginsDoNotUseUpAttempts()
    {
        using var factory = new AuthApiFactory(settings => settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "1");
        using var client = factory.CreateApiClient();

        for (var i = 0; i < 3; i++)
        {
            Assert.Equal(HttpStatusCode.Forbidden, await StatusAsync(client, AuthApiFactory.Json(HttpMethod.Post, "/api/setup", "{}", "https://evil.example")));
        }

        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup()));
    }

    [Fact]
    public async Task ConcurrentPasswordChecksBeyondTheCapAreRejected()
    {
        using var factory = new AuthApiFactory(settings =>
        {
            settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "100";
            settings[AuthRateLimitOptions.ConcurrentPasswordChecksKey] = "1";
            settings[AuthRateLimitOptions.QueuedPasswordChecksKey] = "0";
        });
        using var client = factory.CreateApiClient();
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup()));

        // Each login runs a full 600,000-iteration hash, so overlapping requests exceed a cap of one.
        var responses = await Task.WhenAll(Enumerable.Range(0, 6).Select(i =>
            client.SendAsync(AuthApiFactory.Login(password: $"wrong-password-{i}"), TestContext.Current.CancellationToken)));

        Assert.All(responses, response => Assert.Contains(response.StatusCode, new[] { HttpStatusCode.Unauthorized, HttpStatusCode.TooManyRequests }));
        Assert.Contains(responses, response => response.StatusCode == HttpStatusCode.Unauthorized);
        var rejected = responses.Where(response => response.StatusCode == HttpStatusCode.TooManyRequests).ToList();
        Assert.NotEmpty(rejected);
        Assert.All(rejected, response => Assert.Equal("1", Assert.Single(response.Headers.GetValues("Retry-After"))));
        foreach (var response in responses)
        {
            response.Dispose();
        }
    }

    private static async Task<HttpStatusCode> StatusAsync(HttpClient client, HttpRequestMessage request)
    {
        using (request)
        using (var response = await client.SendAsync(request, TestContext.Current.CancellationToken))
        {
            return response.StatusCode;
        }
    }
}
