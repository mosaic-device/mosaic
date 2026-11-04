using System.Security.Cryptography;

namespace Mosaic.Server.Devices;

/// <summary>A live pairing code for one credential, and when it stops being claimable.</summary>
public sealed record PendingCode(string Code, DateTimeOffset ExpiresAt);

/// <summary>What a claim of a live code hands over: the credential to pair and the MAC it reported.</summary>
public sealed record PendingPairing(byte[] SecretHash, string MacAddress);

/// <summary>
/// Pairing codes for unknown credentials (Device Connection and Pairing Flow, section 2). Codes
/// live only in memory, so a restart drops them and a reconnecting device gets a new one. A
/// credential keeps its code through brief reconnects until it expires. Codes are separate from
/// the admin setup code and are never logged.
/// </summary>
public sealed class PairingCodes(TimeProvider time, DeviceConnectionOptions options)
{
    public static readonly TimeSpan Lifetime = TimeSpan.FromMinutes(10);

    public const int CodeLength = 8;

    // The setup-code alphabet: no look-alikes, so 0/O and 1/I are excluded.
    private const string Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    private readonly Lock _lock = new();
    private readonly Dictionary<string, Entry> _byCode = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Entry> _byCredential = new(StringComparer.Ordinal);

    /// <summary>The number of codes that can still be claimed.</summary>
    public int LiveCount
    {
        get
        {
            lock (_lock)
            {
                RemoveExpired(time.GetUtcNow());
                return _byCode.Count;
            }
        }
    }

    /// <summary>
    /// Returns the credential's live code, or issues a new one. Returns null when the
    /// <see cref="DeviceConnectionOptions.MaxPendingPairings"/> limit is reached.
    /// </summary>
    /// <remarks>
    /// A code with under a second left counts as expired, so a device is never told it has
    /// zero seconds (Device Connection Protocol, section 4).
    /// </remarks>
    public PendingCode? Issue(byte[] secretHash, string macAddress)
    {
        var key = Convert.ToHexString(secretHash);
        lock (_lock)
        {
            var now = time.GetUtcNow();
            RemoveExpired(now);
            if (_byCredential.TryGetValue(key, out var existing))
            {
                return existing.Pending;
            }

            if (_byCode.Count >= options.MaxPendingPairings)
            {
                return null;
            }

            string code;
            do
            {
                code = RandomNumberGenerator.GetString(Alphabet, CodeLength);
            }
            while (_byCode.ContainsKey(code));

            var entry = new Entry(new PendingCode(code, now + Lifetime), key, new PendingPairing(secretHash, macAddress));
            _byCode.Add(code, entry);
            _byCredential.Add(key, entry);
            return entry.Pending;
        }
    }

    /// <summary>
    /// Consumes a live code, case-insensitively and with an optional dash, so it can be claimed
    /// only once. Returns null for an unknown or expired code.
    /// </summary>
    public PendingPairing? TryConsume(string code)
    {
        var normalized = code.Trim().Replace("-", "", StringComparison.Ordinal).ToUpperInvariant();
        lock (_lock)
        {
            RemoveExpired(time.GetUtcNow());
            if (!_byCode.Remove(normalized, out var entry))
            {
                return null;
            }

            _byCredential.Remove(entry.CredentialKey);
            return entry.Pairing;
        }
    }

    /// <summary>Seconds left on <paramref name="code"/> to send to the device, at least 1.</summary>
    public int SecondsLeft(PendingCode code) =>
        Math.Max(1, (int)Math.Floor((code.ExpiresAt - time.GetUtcNow()).TotalSeconds));

    private void RemoveExpired(DateTimeOffset now)
    {
        List<Entry>? expired = null;
        foreach (var entry in _byCode.Values)
        {
            if (entry.Pending.ExpiresAt - now < TimeSpan.FromSeconds(1))
            {
                (expired ??= []).Add(entry);
            }
        }

        foreach (var entry in expired ?? [])
        {
            _byCode.Remove(entry.Pending.Code);
            _byCredential.Remove(entry.CredentialKey);
        }
    }

    private sealed record Entry(PendingCode Pending, string CredentialKey, PendingPairing Pairing);
}
