using System.Globalization;
using System.Security.Cryptography;

namespace Mosaic.Server.Auth;

/// <summary>
/// PBKDF2-HMAC-SHA256 password hashes stored as <c>pbkdf2-sha256$iterations$salt$hash</c>
/// (Base64), so the cost can be raised later without breaking existing hashes.
/// </summary>
public sealed class PasswordHasher
{
    // OWASP Password Storage Cheat Sheet recommendation for PBKDF2-HMAC-SHA256.
    public const int DefaultIterations = 600_000;

    // Stored work factors outside these bounds are treated as corrupt: too low is unsafe,
    // too high would let a tampered row burn CPU on every login.
    public const int MinIterations = 10_000;
    public const int MaxIterations = 10_000_000;

    public const int SaltSize = 16;
    public const int HashSize = 32;

    private const string Scheme = "pbkdf2-sha256";

    private readonly Lazy<string> _dummyHash;

    public PasswordHasher(int iterations = DefaultIterations)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(iterations, MinIterations);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(iterations, MaxIterations);
        Iterations = iterations;

        // Verified against when the username is unknown, so a miss costs the same time as a wrong password.
        _dummyHash = new(() => Hash(Convert.ToBase64String(RandomNumberGenerator.GetBytes(SaltSize))));
    }

    /// <summary>The work factor used for new hashes.</summary>
    public int Iterations { get; }

    public string Hash(string password)
    {
        var salt = RandomNumberGenerator.GetBytes(SaltSize);
        var hash = Rfc2898DeriveBytes.Pbkdf2(password, salt, Iterations, HashAlgorithmName.SHA256, HashSize);
        return string.Join('$', Scheme, Iterations.ToString(CultureInfo.InvariantCulture), Convert.ToBase64String(salt), Convert.ToBase64String(hash));
    }

    /// <summary>Checks a password against a stored hash. Malformed or out-of-bounds hashes never verify.</summary>
    public bool Verify(string password, string stored)
    {
        if (!TryParse(stored, out var iterations, out var salt, out var expected))
        {
            return false;
        }

        var actual = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, HashSize);
        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }

    /// <summary>Spends the same work as <see cref="Verify"/> and always fails.</summary>
    public bool VerifyAgainstDummy(string password)
    {
        Verify(password, _dummyHash.Value);
        return false;
    }

    private static bool TryParse(string stored, out int iterations, out byte[] salt, out byte[] hash)
    {
        iterations = 0;
        salt = new byte[SaltSize];
        hash = new byte[HashSize];
        var parts = stored.Split('$');
        return parts.Length == 4
            && parts[0] == Scheme
            && int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out iterations)
            && iterations is >= MinIterations and <= MaxIterations
            && Convert.TryFromBase64String(parts[2], salt, out var saltLength) && saltLength == SaltSize
            && Convert.TryFromBase64String(parts[3], hash, out var hashLength) && hashLength == HashSize;
    }
}
