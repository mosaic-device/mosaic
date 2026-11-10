using System.Globalization;

namespace Mosaic.Server.Devices;

/// <summary>
/// Device settings. The transport defaults follow the Device Connection Protocol: a 10-second
/// handshake, and ping/pong every 15 seconds with a 15-second timeout (section 6). The claim
/// limit follows the Admin API Contract, section 5.
/// </summary>
public sealed record DeviceConnectionOptions
{
    public const string Section = "Devices";

    public TimeSpan HandshakeTimeout { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan KeepAliveInterval { get; init; } = TimeSpan.FromSeconds(15);

    public TimeSpan KeepAliveTimeout { get; init; } = TimeSpan.FromSeconds(15);

    /// <summary>
    /// The most live pairing codes at once. Each unpaired connection holds one, and a code
    /// outlives a brief disconnect, so this bounds pending connections and pairing state.
    /// </summary>
    public int MaxPendingPairings { get; init; } = 32;

    /// <summary>Signed-in claim attempts allowed per client per minute, counted apart from setup and login.</summary>
    public int ClaimAttemptsPerMinute { get; init; } = 10;

    public static DeviceConnectionOptions FromConfiguration(IConfiguration configuration)
    {
        var section = configuration.GetSection(Section);
        var defaults = new DeviceConnectionOptions();
        return new DeviceConnectionOptions
        {
            HandshakeTimeout = Duration(section, nameof(HandshakeTimeout), defaults.HandshakeTimeout),
            KeepAliveInterval = Duration(section, nameof(KeepAliveInterval), defaults.KeepAliveInterval),
            KeepAliveTimeout = Duration(section, nameof(KeepAliveTimeout), defaults.KeepAliveTimeout),
            MaxPendingPairings = Count(section, nameof(MaxPendingPairings), defaults.MaxPendingPairings),
            ClaimAttemptsPerMinute = Count(section, nameof(ClaimAttemptsPerMinute), defaults.ClaimAttemptsPerMinute),
        };
    }

    private static TimeSpan Duration(IConfigurationSection section, string key, TimeSpan fallback)
    {
        var value = section[key];
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        if (!TimeSpan.TryParse(value, CultureInfo.InvariantCulture, out var parsed) || parsed <= TimeSpan.Zero)
        {
            throw new InvalidOperationException($"{Section}:{key} must be a positive duration such as 00:00:15.");
        }

        return parsed;
    }

    private static int Count(IConfigurationSection section, string key, int fallback)
    {
        var value = section[key];
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        if (!int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var parsed) || parsed < 1)
        {
            throw new InvalidOperationException($"{Section}:{key} must be a whole number of at least 1.");
        }

        return parsed;
    }
}
