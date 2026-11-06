using System.Security.Cryptography;
using System.Text;

namespace Mosaic.Server.Auth;

/// <summary>
/// The one-time code that authorizes first-run setup. It lives only in memory, so it
/// changes on every restart until an admin exists.
/// </summary>
public sealed class SetupCode
{
    public const string OverrideKey = "MOSAIC_SETUP_CODE";

    // No look-alikes: 0/O and 1/I are excluded.
    private const string Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    private readonly string _normalized;

    private SetupCode(string display, bool fromOverride)
    {
        Display = display;
        FromOverride = fromOverride;
        _normalized = Normalize(display);
    }

    /// <summary>The code as shown to the owner, such as <c>K7QF-2M9X</c>.</summary>
    public string Display { get; }

    /// <summary>Whether the code came from <see cref="OverrideKey"/> rather than being generated.</summary>
    public bool FromOverride { get; }

    public static SetupCode FromConfiguration(IConfiguration configuration)
    {
        var configured = configuration[OverrideKey];
        if (!string.IsNullOrWhiteSpace(configured))
        {
            if (Normalize(configured).Length == 0)
            {
                throw new InvalidOperationException($"{OverrideKey} must contain at least one letter or digit.");
            }

            return new SetupCode(configured.Trim(), fromOverride: true);
        }

        var characters = RandomNumberGenerator.GetString(Alphabet, 8);
        return new SetupCode($"{characters[..4]}-{characters[4..]}", fromOverride: false);
    }

    /// <summary>Case-insensitive, dash-optional, constant-time comparison.</summary>
    public bool Matches(string candidate) =>
        CryptographicOperations.FixedTimeEquals(
            Encoding.UTF8.GetBytes(Normalize(candidate)),
            Encoding.UTF8.GetBytes(_normalized));

    private static string Normalize(string value) =>
        value.Trim().Replace("-", "", StringComparison.Ordinal).ToUpperInvariant();
}
