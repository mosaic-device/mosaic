using System.Net;
using Mosaic.Server.Auth;
using Mosaic.Server.Network;

namespace Mosaic.Server.Tests;

/// <summary>Which client address the per-client attempt limit sees, with and without a trusted proxy.</summary>
public sealed class TrustedProxyTests
{
    private const string Proxy = "127.0.0.1";
    private const string Outsider = "203.0.113.50";
    private const string ClientA = "198.51.100.1";
    private const string ClientB = "198.51.100.2";

    [Fact]
    public async Task ForwardedForIsIgnoredWithoutTrustedProxies()
    {
        using var factory = OneAttemptPerMinute();
        using var client = factory.CreateApiClient();

        // A direct client cannot get a fresh limit by naming a different address.
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup(), peer: Outsider, forwardedFor: ClientA));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, AuthApiFactory.Login(), peer: Outsider, forwardedFor: ClientB));
    }

    [Fact]
    public async Task ForwardedForFromAnUntrustedPeerIsIgnored()
    {
        using var factory = OneAttemptPerMinute(Proxy);
        using var client = factory.CreateApiClient();

        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup(), peer: Outsider, forwardedFor: ClientA));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, AuthApiFactory.Login(), peer: Outsider, forwardedFor: ClientB));
    }

    [Fact]
    public async Task ATrustedProxyGivesEachClientItsOwnLimit()
    {
        using var factory = OneAttemptPerMinute(Proxy);
        using var client = factory.CreateApiClient();

        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup(), peer: Proxy, forwardedFor: ClientA));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, AuthApiFactory.Login(), peer: Proxy, forwardedFor: ClientA));

        // Another client behind the same proxy still has its attempt.
        Assert.Equal(HttpStatusCode.OK, await StatusAsync(client, AuthApiFactory.Login(), peer: Proxy, forwardedFor: ClientB));
    }

    [Fact]
    public async Task OnlyTheAddressTheProxyAddedCounts()
    {
        using var factory = OneAttemptPerMinute(Proxy);
        using var client = factory.CreateApiClient();

        // Addresses a client prepends are not the proxy's to vouch for.
        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup(), peer: Proxy, forwardedFor: $"192.0.2.1, {ClientA}"));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, AuthApiFactory.Login(), peer: Proxy, forwardedFor: $"192.0.2.2, {ClientA}"));
    }

    [Fact]
    public async Task AnIPv4ProxySeenThroughADualStackSocketIsTrusted()
    {
        // Kestrel listening on [::] (as in the container) reports IPv4 peers as IPv4-mapped IPv6 addresses.
        using var factory = OneAttemptPerMinute("172.17.0.1");
        using var client = factory.CreateApiClient();

        Assert.Equal(HttpStatusCode.Created, await StatusAsync(client, AuthApiFactory.Setup(), peer: "::ffff:172.17.0.1", forwardedFor: ClientA));
        Assert.Equal(HttpStatusCode.OK, await StatusAsync(client, AuthApiFactory.Login(), peer: "::ffff:172.17.0.1", forwardedFor: ClientB));
    }

    [Theory]
    [InlineData("5000")]
    [InlineData("127.1")]
    [InlineData("172.17.0.0/16")]
    [InlineData("localhost")]
    public void StartupFailsOnAnInvalidProxyAddress(string value)
    {
        using var factory = new AuthApiFactory(settings => settings[TrustedProxies.KnownProxiesKey + ":0"] = value);

        var error = Assert.ThrowsAny<Exception>(() => factory.CreateApiClient());

        Assert.Contains(TrustedProxies.KnownProxiesKey, Flatten(error), StringComparison.Ordinal);
    }

    [Fact]
    public void StartupFailsWhenAnyProxyWouldBeTrusted()
    {
        using var factory = new AuthApiFactory(settings => settings["FORWARDEDHEADERS_ENABLED"] = "true");

        var error = Assert.ThrowsAny<Exception>(() => factory.CreateApiClient());

        Assert.Contains("ASPNETCORE_FORWARDEDHEADERS_ENABLED", Flatten(error), StringComparison.Ordinal);
    }

    private static AuthApiFactory OneAttemptPerMinute(params string[] proxies) => new(settings =>
    {
        settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "1";
        for (var i = 0; i < proxies.Length; i++)
        {
            settings[$"{TrustedProxies.KnownProxiesKey}:{i}"] = proxies[i];
        }
    });

    private static async Task<HttpStatusCode> StatusAsync(HttpClient client, HttpRequestMessage request, string peer, string forwardedFor)
    {
        request.Headers.Add(AuthApiFactory.PeerHeader, peer);
        request.Headers.Add("X-Forwarded-For", forwardedFor);
        using (request)
        using (var response = await client.SendAsync(request, TestContext.Current.CancellationToken))
        {
            return response.StatusCode;
        }
    }

    private static string Flatten(Exception error) =>
        error.InnerException is null ? error.Message : error.Message + " | " + Flatten(error.InnerException);
}
