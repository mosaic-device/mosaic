using System.Net;
using System.Net.Sockets;
using Microsoft.AspNetCore.HttpOverrides;

namespace Mosaic.Server.Network;

/// <summary>
/// Reverse proxies whose <c>X-Forwarded-For</c> header is trusted, such as tailscaled for Tailscale Funnel.
/// None by default, so a directly exposed server ignores the header and clients cannot choose their own address.
/// </summary>
public sealed class TrustedProxies
{
    public const string KnownProxiesKey = "ForwardedHeaders:KnownProxies";

    // ASPNETCORE_FORWARDEDHEADERS_ENABLED, which trusts X-Forwarded-For from any peer.
    private const string TrustAnyProxyKey = "FORWARDEDHEADERS_ENABLED";

    private TrustedProxies(IReadOnlyList<IPAddress> addresses) => Addresses = addresses;

    /// <summary>The immediate peers allowed to report the client address.</summary>
    public IReadOnlyList<IPAddress> Addresses { get; }

    public static TrustedProxies FromConfiguration(IConfiguration configuration)
    {
        if (string.Equals(configuration[TrustAnyProxyKey], "true", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                $"ASPNETCORE_FORWARDEDHEADERS_ENABLED trusts X-Forwarded-For from any client. Unset it and list the proxy in {KnownProxiesKey} instead.");
        }

        var addresses = new List<IPAddress>();
        foreach (var child in configuration.GetSection(KnownProxiesKey).GetChildren())
        {
            if (!string.IsNullOrWhiteSpace(child.Value))
            {
                addresses.Add(Parse(child.Value.Trim()));
            }
        }

        return new TrustedProxies(addresses);
    }

    /// <summary>Uses the last <c>X-Forwarded-For</c> address, and only from a listed proxy.</summary>
    public void Configure(ForwardedHeadersOptions options)
    {
        options.ForwardedHeaders = ForwardedHeaders.XForwardedFor;
        options.ForwardLimit = 1;

        // The framework trusts loopback by default; trust only what is configured.
        options.KnownProxies.Clear();
        options.KnownIPNetworks.Clear();
        foreach (var address in Addresses)
        {
            options.KnownProxies.Add(address);
        }
    }

    private static IPAddress Parse(string value)
    {
        // IPAddress.TryParse also accepts shorthand such as "5000" (0.0.19.136); require a full address.
        if (!IPAddress.TryParse(value, out var address)
            || (address.AddressFamily == AddressFamily.InterNetwork && value.Count(c => c == '.') != 3))
        {
            throw new InvalidOperationException($"{KnownProxiesKey} entries must be IP addresses such as 127.0.0.1, not '{value}'.");
        }

        return address;
    }
}

public static class TrustedProxyExtensions
{
    public static IServiceCollection AddTrustedProxies(this IServiceCollection services)
    {
        services.AddSingleton(provider => TrustedProxies.FromConfiguration(provider.GetRequiredService<IConfiguration>()));
        services.AddOptions<ForwardedHeadersOptions>().Configure<TrustedProxies>((options, proxies) => proxies.Configure(options));
        return services;
    }

    /// <summary>
    /// Replaces the connection's remote address with the forwarded client address when a trusted proxy sent it.
    /// Must run before anything that reads the remote address, such as the rate limiter.
    /// </summary>
    public static IApplicationBuilder UseTrustedProxies(this IApplicationBuilder app)
    {
        var proxies = app.ApplicationServices.GetRequiredService<TrustedProxies>();

        // With no known proxies the middleware would trust every peer, so leave it out entirely.
        return proxies.Addresses.Count == 0 ? app : app.UseForwardedHeaders();
    }
}
