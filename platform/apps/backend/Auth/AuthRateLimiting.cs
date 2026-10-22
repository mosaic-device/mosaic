using System.Globalization;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.Extensions.Options;

namespace Mosaic.Server.Auth;

/// <summary>Limits on setup and login attempts, which each run an expensive password hash.</summary>
public sealed class AuthRateLimitOptions
{
    public const string AttemptsPerMinuteKey = "Auth:RateLimits:AttemptsPerMinute";
    public const string ConcurrentPasswordChecksKey = "Auth:RateLimits:ConcurrentPasswordChecks";
    public const string QueuedPasswordChecksKey = "Auth:RateLimits:QueuedPasswordChecks";
    public const string BodyReadTimeoutSecondsKey = "Auth:RateLimits:BodyReadTimeoutSeconds";

    /// <summary>Setup and login attempts allowed per client per minute, combined.</summary>
    public int AttemptsPerMinute { get; private init; } = 10;

    /// <summary>Password hashes allowed to run at once across all clients, bounding CPU use.</summary>
    public int ConcurrentPasswordChecks { get; private init; } = Math.Max(1, Environment.ProcessorCount / 2);

    /// <summary>Attempts allowed to wait for a hashing slot before being rejected.</summary>
    public int QueuedPasswordChecks { get; private init; } = 16;

    /// <summary>How long a setup or login body may take to arrive before the connection is closed.</summary>
    public TimeSpan BodyReadTimeout { get; private init; } = TimeSpan.FromSeconds(10);

    public static AuthRateLimitOptions FromConfiguration(IConfiguration configuration)
    {
        var defaults = new AuthRateLimitOptions();
        return new AuthRateLimitOptions
        {
            AttemptsPerMinute = Read(configuration, AttemptsPerMinuteKey, defaults.AttemptsPerMinute, minimum: 1),
            ConcurrentPasswordChecks = Read(configuration, ConcurrentPasswordChecksKey, defaults.ConcurrentPasswordChecks, minimum: 1),
            QueuedPasswordChecks = Read(configuration, QueuedPasswordChecksKey, defaults.QueuedPasswordChecks, minimum: 0),
            BodyReadTimeout = TimeSpan.FromSeconds(
                Read(configuration, BodyReadTimeoutSecondsKey, (int)defaults.BodyReadTimeout.TotalSeconds, minimum: 1)),
        };
    }

    private static int Read(IConfiguration configuration, string key, int fallback, int minimum)
    {
        var value = configuration[key];
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        if (!int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) || parsed < minimum)
        {
            throw new InvalidOperationException($"{key} must be a whole number of at least {minimum}, not '{value}'.");
        }

        return parsed;
    }
}

/// <summary>
/// Rate limiting for endpoints that check passwords. Two independent limits apply:
/// a per-client attempt window, enforced here when a request is admitted, and a global
/// cap on concurrent hashing, enforced by <see cref="PasswordCheckLimiter"/> around the
/// hashing itself. Either one rejects with <c>429 too_many_requests</c> and a <c>Retry-After</c> header.
/// </summary>
public static class AuthRateLimiting
{
    public const string AttemptsPolicy = "auth-attempts";

    private static readonly TimeSpan AttemptWindow = TimeSpan.FromMinutes(1);

    public static IServiceCollection AddAuthRateLimiting(this IServiceCollection services)
    {
        services.AddSingleton(provider => AuthRateLimitOptions.FromConfiguration(provider.GetRequiredService<IConfiguration>()));
        services.AddSingleton<PasswordCheckLimiter>();
        services.AddRateLimiter(_ => { });
        services.AddOptions<RateLimiterOptions>().Configure<AuthRateLimitOptions>(Configure);
        return services;
    }

    /// <summary>Marks an endpoint as a password check, subject to the per-client attempt limit.</summary>
    public static TBuilder RequireAuthRateLimits<TBuilder>(this TBuilder builder)
        where TBuilder : IEndpointConventionBuilder => builder.RequireRateLimiting(AttemptsPolicy);

    private static void Configure(RateLimiterOptions options, AuthRateLimitOptions limits)
    {
        options.RejectionStatusCode = StatusCodes.Status429TooManyRequests;

        // Partitioned by client address. Behind a proxy such as Tailscale Funnel this is the forwarded
        // client address only when the proxy is listed in ForwardedHeaders:KnownProxies; otherwise every
        // request shares the proxy's address and this becomes one shared limit.
        options.AddPolicy(AttemptsPolicy, context => RateLimitPartition.GetFixedWindowLimiter(
            context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = limits.AttemptsPerMinute,
                Window = AttemptWindow,
                QueueLimit = 0,
            }));

        options.OnRejected = async (rejected, _) =>
        {
            var retryAfter = rejected.Lease.TryGetMetadata(MetadataName.RetryAfter, out var wait)
                ? Math.Max(1, (int)Math.Ceiling(wait.TotalSeconds))
                : 1;
            await Problems.TooManyRequests(rejected.HttpContext, retryAfter).ExecuteAsync(rejected.HttpContext);
        };
    }
}
