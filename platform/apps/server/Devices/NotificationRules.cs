using System.Text;
using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>
/// The field rules a notification must meet before the server sends it (Notification Delivery
/// Protocol, section 3). The C# bindings don't enforce the nanopb limits, so the server checks
/// them; the device checks again and answers <c>INVALID</c> if one is broken.
/// </summary>
/// <remarks>
/// Byte limits are UTF-8 bytes without the terminating NUL that nanopb counts. Shortening text
/// to fit is the producer's job; this only says whether a notification may be sent.
/// </remarks>
public static class NotificationRules
{
    public const int MaxIdLength = 63;
    public const int MaxSourceBytes = 31;
    public const int MaxTitleBytes = 127;
    public const int MaxTextBytes = 1023;

    /// <summary>Null when <paramref name="notification"/> may be sent, otherwise which rule it breaks.</summary>
    public static string? Check(Notification notification)
    {
        var id = notification.Id;
        if (id.Length is 0 or > MaxIdLength || !id.All(character => character is >= ' ' and <= '~'))
        {
            return $"The ID must be 1 to {MaxIdLength} printable ASCII characters.";
        }

        if (notification.Priority is not (NotificationPriority.Normal or NotificationPriority.High))
        {
            return "The priority must be NORMAL or HIGH.";
        }

        if (Bytes(notification.Source) is 0 or > MaxSourceBytes)
        {
            return $"The source must be 1 to {MaxSourceBytes} bytes of UTF-8.";
        }

        if (Bytes(notification.Title) is 0 or > MaxTitleBytes)
        {
            return $"The title must be 1 to {MaxTitleBytes} bytes of UTF-8.";
        }

        if (Bytes(notification.Text) > MaxTextBytes)
        {
            return $"The text must be at most {MaxTextBytes} bytes of UTF-8.";
        }

        return notification.OccurredAt > 0 ? null : "The time it occurred must be after 1970.";
    }

    private static int Bytes(string value) => Encoding.UTF8.GetByteCount(value);
}
