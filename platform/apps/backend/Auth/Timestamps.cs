using System.Globalization;

namespace Mosaic.Server.Auth;

internal static class Timestamps
{
    // Same shape as the migration timestamps, so stored values sort and compare as text.
    public static string Format(DateTimeOffset value) =>
        value.UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", CultureInfo.InvariantCulture);
}
