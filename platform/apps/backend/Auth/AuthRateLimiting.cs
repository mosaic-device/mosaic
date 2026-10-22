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

    /// <summary>Setup and login attempts allowed per client per minute, combined.</summary>
    public int AttemptsPerMinute { get; private init; } = 10;

    /// <summary>Password hashes allowed to run at once across all clients, bounding CPU use.</summary>
    public int ConcurrentPasswordChecks { get; private init; } = Math.Max(1, Environment.ProcessorCount / 2);

    /// <summary>Attempts allowed to wait for a hashing slot before being rejected.</summary>
    public int QueuedPasswordChecks { get; private init; } = 16;

    public static AuthRateLimitOptions FromConfiguration(IConfiguration configuration)
    {
        var defaults = new AuthRateLimitOptions();
        return new AuthRateLimitOptions
        {
            AttemptsPerMinute = Read(configuration, AttemptsPerMinuteKey, defaults.AttemptsPerMinute, minimum: 1),
            ConcurrentPasswordChecks = Read(configuration, ConcurrentPasswordChecksKey, defaults.ConcurrentPasswordChecks, minimum: 1),
            QueuedPasswordChecks = Read(configuration, QueuedPasswordChecksKey, defaults.QueuedPasswordChecks, minimum: 0),
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
/// a per-client attempt window, and a global cap on concurrent hashing. Either one
/// rejects with <c>429 too_many_requests</c> and a <c>Retry-After</c> header.
/// </summary>
public static class AuthRateLimiting
{
    public const string AttemptsPolicy = "auth-attempts";

    private static readonly TimeSpan AttemptWindow = TimeSpan.FromMinutes(1);

    public static IServiceCollection AddAuthRateLimiting(this IServiceCollection services)
    {
        services.AddSingleton(provider => AuthRateLimitOptions.FromConfiguration(provider.GetRequiredService<IConfiguration>()));
        services.AddRateLimiter(_ => { });
        services.AddOptions<RateLimiterOptions>().Configure<AuthRateLimitOptions>(Configure);
        return services;
    }

    /// <summary>Marks an endpoint as a password check, subject to both limits.</summary>
    public static TBuilder RequireAuthRateLimits<TBuilder>(this TBuilder builder)
        where TBuilder : IEndpointConventionBuilder => builder.RequireRateLimiting(AttemptsPolicy);

    private static void Configure(RateLimiterOptions options, AuthRateLimitOptions limits)
    {
        options.RejectionStatusCode = StatusCodes.Status429TooManyRequests;

        // Partitioned by client address. Behind a proxy such as Tailscale Funnel every request
        // shares the proxy's address, so this becomes one shared limit until forwarded headers are trusted.
        options.AddPolicy(AttemptsPolicy, context => RateLimitPartition.GetFixedWindowLimiter(
            context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
            _ => new FixedWindowRateLimiterOptions
            {
                PermitLimit = limits.AttemptsPerMinute,
                Window = AttemptWindow,
                QueueLimit = 0,
            }));

        // One shared partition: the cap is on total hashing work, whoever sends it.
        options.GlobalLimiter = PartitionedRateLimiter.Create<HttpContext, string>(context =>
            IsPasswordCheck(context)
                ? RateLimitPartition.GetConcurrencyLimiter("password-checks", _ => new ConcurrencyLimiterOptions
                {
                    PermitLimit = limits.ConcurrentPasswordChecks,
                    QueueLimit = limits.QueuedPasswordChecks,
                    QueueProcessingOrder = QueueProcessingOrder.OldestFirst,
                })
                : RateLimitPartition.GetNoLimiter(""));

        options.OnRejected = async (rejected, _) =>
        {
            var retryAfter = rejected.Lease.TryGetMetadata(MetadataName.RetryAfter, out var wait)
                ? Math.Max(1, (int)Math.Ceiling(wait.TotalSeconds))
                : 1;
            rejected.HttpContext.Response.Headers.RetryAfter = retryAfter.ToString(CultureInfo.InvariantCulture);
            await Problems.TooManyRequests(rejected.HttpContext).ExecuteAsync(rejected.HttpContext);
        };
    }

    private static bool IsPasswordCheck(HttpContext context) =>
        context.GetEndpoint()?.Metadata.GetMetadata<EnableRateLimitingAttribute>()?.PolicyName == AttemptsPolicy;
}
