using System.Globalization;

namespace Mosaic.Server.Auth;

internal static class Timestamps
{
    // Same shape as the migration timestamps, so stored values sort and compare as text.
    private const string StoredFormat = "yyyy-MM-dd'T'HH:mm:ss.fff'Z'";

    public static string Format(DateTimeOffset value) =>
        value.UtcDateTime.ToString(StoredFormat, CultureInfo.InvariantCulture);

    /// <summary>The UTC time rounded down to a whole second, as the admin API returns times.</summary>
    public static DateTime WholeSeconds(DateTimeOffset value)
    {
        var utc = value.UtcDateTime;
        return utc.AddTicks(-(utc.Ticks % TimeSpan.TicksPerSecond));
    }

    public static DateTimeOffset Parse(string value) =>
        DateTimeOffset.ParseExact(value, StoredFormat, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal);
}
