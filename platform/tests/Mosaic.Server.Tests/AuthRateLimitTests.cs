using System.Net;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
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
    public async Task PasswordChecksBeyondTheCapGet429()
    {
        using var factory = SingleHashingSlot();
        using var client = factory.CreateApiClient();
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup()));
        var passwordChecks = factory.Services.GetRequiredService<PasswordCheckLimiter>();

        using (var held = await passwordChecks.AcquireAsync(TestContext.Current.CancellationToken))
        {
            Assert.True(held.IsAcquired);
            using var rejected = await client.SendAsync(AuthApiFactory.Login(), TestContext.Current.CancellationToken);

            Assert.Equal(HttpStatusCode.TooManyRequests, rejected.StatusCode);
            Assert.Equal("1", Assert.Single(rejected.Headers.GetValues("Retry-After")));
            var body = JsonDocument.Parse(await rejected.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement;
            Assert.Equal("too_many_requests", body.GetProperty("code").GetString());
        }

        Assert.Equal(HttpStatusCode.OK, await StatusAsync(client, AuthApiFactory.Login()));
    }

    [Fact]
    public async Task AnUnfinishedUploadDoesNotHoldAHashingSlot()
    {
        using var factory = SingleHashingSlot();
        using var client = factory.CreateApiClient();
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup()));

        // A login whose body is still arriving must not block another client's password check.
        var stalled = new StalledJsonContent("""{"username":"Bridger","password":"mosaic-desk-2026"}""");
        using var slowRequest = new HttpRequestMessage(HttpMethod.Post, "/api/auth/login") { Content = stalled };
        slowRequest.Headers.Add("Origin", AuthApiFactory.PublicOrigin);
        var slowResponse = client.SendAsync(slowRequest, TestContext.Current.CancellationToken);
        await stalled.FirstChunkSent.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.Unauthorized, await StatusAsync(client, AuthApiFactory.Login(password: "wrong-password-1")));

        stalled.Finish();
        using var finished = await slowResponse;
        Assert.Equal(HttpStatusCode.OK, finished.StatusCode);
    }

    private static AuthApiFactory SingleHashingSlot() => new(settings =>
    {
        settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "100";
        settings[AuthRateLimitOptions.ConcurrentPasswordChecksKey] = "1";
        settings[AuthRateLimitOptions.QueuedPasswordChecksKey] = "0";
    });

    private static async Task<HttpStatusCode> StatusAsync(HttpClient client, HttpRequestMessage request)
    {
        using (request)
        using (var response = await client.SendAsync(request, TestContext.Current.CancellationToken))
        {
            return response.StatusCode;
        }
    }
}
