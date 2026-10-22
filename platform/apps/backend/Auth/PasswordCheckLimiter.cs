using System.Threading.RateLimiting;

namespace Mosaic.Server.Auth;

/// <summary>
/// Caps how many password hashes run at once across all clients, bounding CPU use.
/// Permits are taken only around the hashing itself, never while a request body is
/// still arriving, so a slow upload cannot hold a slot.
/// </summary>
public sealed class PasswordCheckLimiter(AuthRateLimitOptions limits) : IDisposable
{
    private readonly ConcurrencyLimiter _limiter = new(new ConcurrencyLimiterOptions
    {
        PermitLimit = limits.ConcurrentPasswordChecks,
        QueueLimit = limits.QueuedPasswordChecks,
        QueueProcessingOrder = QueueProcessingOrder.OldestFirst,
    });

    /// <summary>Waits for a slot; the lease is not acquired when the queue is full.</summary>
    public ValueTask<RateLimitLease> AcquireAsync(CancellationToken cancellationToken) =>
        _limiter.AcquireAsync(1, cancellationToken);

    public void Dispose() => _limiter.Dispose();
}
