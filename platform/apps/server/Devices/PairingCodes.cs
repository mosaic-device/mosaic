using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;

namespace Mosaic.Server.Devices;

/// <summary>A live pairing code for one credential, and when it stops being claimable.</summary>
public sealed record PendingCode(string Code, DateTimeOffset ExpiresAt);

public enum CodeState
{
    /// <summary>Never issued, from before a restart, or forgotten after <see cref="PairingCodes.Retention"/>.</summary>
    Unknown,

    /// <summary>Issued and still claimable.</summary>
    Live,

    /// <summary>Claimed. Remembered so a retried claim resolves to the same device.</summary>
    Used,

    /// <summary>Ran out before it was claimed. Remembered so the admin is told it expired.</summary>
    Expired,
}

/// <summary>What a code is now, and the credential it was issued to when it is remembered at all.</summary>
public sealed record CodeLookup(CodeState State, byte[]? SecretHash);

/// <summary>
/// Pairing codes for unknown credentials (Device Connection and Pairing Flow, section 2). Codes
/// live only in memory, so a restart drops them and a reconnecting device gets a new one. A
/// credential keeps its code through brief reconnects until it expires. Used and expired codes
/// are remembered for at least <see cref="Retention"/> so a claim can tell them apart (Admin API
/// Contract, section 5). Codes are separate from the admin setup code and are never logged.
/// </summary>
/// <remarks>
/// This only tracks codes. <see cref="DevicePairing"/> decides when to issue and claim them,
/// together with the stored credential.
/// </remarks>
public sealed class PairingCodes(TimeProvider time, DeviceConnectionOptions options)
{
    public static readonly TimeSpan Lifetime = TimeSpan.FromMinutes(10);

    /// <summary>How long a used or expired code is remembered after it stops being live.</summary>
    public static readonly TimeSpan Retention = TimeSpan.FromHours(1);

    public const int CodeLength = 8;

    // The setup-code alphabet: no look-alikes, so 0/O and 1/I are excluded.
    private const string Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    private readonly Lock _lock = new();
    private readonly Dictionary<string, Entry> _live = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Entry> _liveByCredential = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Remembered> _remembered = new(StringComparer.Ordinal);

    /// <summary>The number of codes that can still be claimed.</summary>
    public int LiveCount
    {
        get
        {
            lock (_lock)
            {
                Tidy(time.GetUtcNow());
                return _live.Count;
            }
        }
    }

    /// <summary>The number of used and expired codes still remembered.</summary>
    public int RememberedCount
    {
        get
        {
            lock (_lock)
            {
                Tidy(time.GetUtcNow());
                return _remembered.Count;
            }
        }
    }

    /// <summary>
    /// Puts a code as an admin typed it into its stored form: spaces trimmed from both ends,
    /// dashes removed and uppercased. False unless the result is <see cref="CodeLength"/>
    /// characters from the code alphabet.
    /// </summary>
    public static bool TryNormalize(string? input, [NotNullWhen(true)] out string? code)
    {
        code = null;
        if (input is null)
        {
            return false;
        }

        var normalized = input.Trim(' ').Replace("-", "", StringComparison.Ordinal).ToUpperInvariant();
        if (normalized.Length != CodeLength || !normalized.All(Alphabet.Contains))
        {
            return false;
        }

        code = normalized;
        return true;
    }

    /// <summary>
    /// Returns the credential's live code, or issues a new one that matches no live or remembered
    /// code. Returns null when <see cref="DeviceConnectionOptions.MaxPendingPairings"/> codes are
    /// live; remembered codes don't count.
    /// </summary>
    /// <remarks>
    /// A code with under a second left counts as expired, so a device is never told it has
    /// zero seconds (Device Connection Protocol, section 4).
    /// </remarks>
    public PendingCode? Issue(byte[] secretHash)
    {
        var key = Convert.ToHexString(secretHash);
        lock (_lock)
        {
            var now = time.GetUtcNow();
            Tidy(now);
            if (_liveByCredential.TryGetValue(key, out var existing))
            {
                return existing.Pending;
            }

            if (_live.Count >= options.MaxPendingPairings)
            {
                return null;
            }

            string code;
            do
            {
                code = RandomNumberGenerator.GetString(Alphabet, CodeLength);
            }
            while (_live.ContainsKey(code) || _remembered.ContainsKey(code));

            var entry = new Entry(new PendingCode(code, now + Lifetime), key, secretHash);
            _live.Add(code, entry);
            _liveByCredential.Add(key, entry);
            return entry.Pending;
        }
    }

    /// <summary>Looks up a code already put in stored form by <see cref="TryNormalize"/>.</summary>
    public CodeLookup Find(string code)
    {
        lock (_lock)
        {
            Tidy(time.GetUtcNow());
            if (_live.TryGetValue(code, out var live))
            {
                return new CodeLookup(CodeState.Live, live.SecretHash);
            }

            return _remembered.TryGetValue(code, out var remembered)
                ? new CodeLookup(remembered.State, remembered.Entry.SecretHash)
                : new CodeLookup(CodeState.Unknown, null);
        }
    }

    /// <summary>
    /// Marks a code that <see cref="Find"/> just reported live as claimed, so it is remembered as
    /// used. Call only once the pairing it hands over is committed. The claim was accepted while
    /// the code was live, so a code that expired during the commit is still marked used, and a
    /// retry gets the same device rather than <c>410</c>. Does nothing for any other code.
    /// </summary>
    public void MarkUsed(string code)
    {
        lock (_lock)
        {
            var now = time.GetUtcNow();
            Entry entry;
            if (_live.Remove(code, out var live))
            {
                _liveByCredential.Remove(live.CredentialKey);
                entry = live;
            }
            else if (_remembered.TryGetValue(code, out var remembered) && remembered.State == CodeState.Expired)
            {
                entry = remembered.Entry;
            }
            else
            {
                Tidy(now);
                return;
            }

            _remembered[code] = new Remembered(entry, CodeState.Used, now + Retention);
            Tidy(now);
        }
    }

    /// <summary>Seconds left on <paramref name="code"/> to send to the device, at least 1.</summary>
    public int SecondsLeft(PendingCode code) =>
        Math.Max(1, (int)Math.Floor((code.ExpiresAt - time.GetUtcNow()).TotalSeconds));

    /// <summary>Moves expired live codes to the remembered ones, and forgets those past retention.</summary>
    private void Tidy(DateTimeOffset now)
    {
        List<Entry>? expired = null;
        foreach (var entry in _live.Values)
        {
            if (entry.Pending.ExpiresAt - now < TimeSpan.FromSeconds(1))
            {
                (expired ??= []).Add(entry);
            }
        }

        foreach (var entry in expired ?? [])
        {
            _live.Remove(entry.Pending.Code);
            _liveByCredential.Remove(entry.CredentialKey);
            // Counted from the expiry itself, not from when it was noticed.
            _remembered[entry.Pending.Code] = new Remembered(entry, CodeState.Expired, entry.Pending.ExpiresAt + Retention);
        }

        List<string>? forgotten = null;
        foreach (var (code, remembered) in _remembered)
        {
            if (remembered.ForgetAt <= now)
            {
                (forgotten ??= []).Add(code);
            }
        }

        foreach (var code in forgotten ?? [])
        {
            _remembered.Remove(code);
        }
    }

    private sealed record Entry(PendingCode Pending, string CredentialKey, byte[] SecretHash);

    private sealed record Remembered(Entry Entry, CodeState State, DateTimeOffset ForgetAt);
}
