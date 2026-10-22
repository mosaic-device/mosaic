using System.Globalization;
using System.Security.Cryptography;

namespace Mosaic.Server.Auth;

/// <summary>
/// PBKDF2-HMAC-SHA256 password hashes stored as <c>pbkdf2-sha256$iterations$salt$hash</c>
/// (Base64), so the cost can be raised later without breaking existing hashes.
/// </summary>
public static class PasswordHasher
{
    // OWASP Password Storage Cheat Sheet recommendation for PBKDF2-HMAC-SHA256.
    public const int Iterations = 600_000;

    private const string Scheme = "pbkdf2-sha256";
    private const int SaltSize = 16;
    private const int HashSize = 32;

    // Verified against when the username is unknown, so a miss costs the same time as a wrong password.
    private static readonly Lazy<string> DummyHash = new(() => Hash(Convert.ToBase64String(RandomNumberGenerator.GetBytes(SaltSize))));

    public static string Hash(string password)
    {
        var salt = RandomNumberGenerator.GetBytes(SaltSize);
        var hash = Rfc2898DeriveBytes.Pbkdf2(password, salt, Iterations, HashAlgorithmName.SHA256, HashSize);
        return string.Join('$', Scheme, Iterations.ToString(CultureInfo.InvariantCulture), Convert.ToBase64String(salt), Convert.ToBase64String(hash));
    }

    public static bool Verify(string password, string stored)
    {
        var parts = stored.Split('$');
        if (parts.Length != 4 || parts[0] != Scheme
            || !int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var iterations) || iterations < 1)
        {
            return false;
        }

        byte[] salt, expected;
        try
        {
            salt = Convert.FromBase64String(parts[2]);
            expected = Convert.FromBase64String(parts[3]);
        }
        catch (FormatException)
        {
            return false;
        }

        var actual = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, expected.Length);
        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }

    /// <summary>Spends the same work as <see cref="Verify"/> and always fails.</summary>
    public static bool VerifyAgainstDummy(string password)
    {
        Verify(password, DummyHash.Value);
        return false;
    }
}
