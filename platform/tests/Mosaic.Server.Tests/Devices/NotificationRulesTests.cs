using Mosaic.Device.V1;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>The field rules a notification must meet before it is sent (Notification Delivery Protocol, section 3).</summary>
public sealed class NotificationRulesTests
{
    [Fact]
    public void TheTestNotificationMeetsEveryRule() =>
        Assert.Null(NotificationRules.Check(DeviceApi.TestNotification(DateTimeOffset.FromUnixTimeSeconds(1793903400))));

    [Fact]
    public void EveryFieldAtItsLimitIsAllowed()
    {
        var notification = Valid();
        notification.Id = new string('~', NotificationRules.MaxIdLength);
        notification.Priority = NotificationPriority.High;
        notification.Source = new string('s', NotificationRules.MaxSourceBytes);
        // 42 three-byte characters and one byte: exactly 127 bytes.
        notification.Title = new string('€', 42) + "t";
        notification.Text = new string('x', NotificationRules.MaxTextBytes);

        Assert.Null(NotificationRules.Check(notification));
        Assert.Null(NotificationRules.Check(new Notification(notification) { Text = "" }));
    }

    public static TheoryData<string, Action<Notification>> Broken() => new()
    {
        { "empty ID", notification => notification.Id = "" },
        { "ID too long", notification => notification.Id = new string('a', NotificationRules.MaxIdLength + 1) },
        { "ID with a control character", notification => notification.Id = "test\n1" },
        { "ID beyond ASCII", notification => notification.Id = "tést" },
        { "no priority", notification => notification.Priority = NotificationPriority.Unspecified },
        { "unknown priority", notification => notification.Priority = (NotificationPriority)7 },
        { "empty source", notification => notification.Source = "" },
        { "source too long", notification => notification.Source = new string('s', NotificationRules.MaxSourceBytes + 1) },
        { "empty title", notification => notification.Title = "" },
        // 128 bytes in 43 characters: under the limit counted in characters, over it in bytes.
        { "title too long in bytes", notification => notification.Title = new string('€', 42) + "tt" },
        { "text too long", notification => notification.Text = new string('x', NotificationRules.MaxTextBytes + 1) },
        { "no time", notification => notification.OccurredAt = 0 },
        { "time before 1970", notification => notification.OccurredAt = -1 },
    };

    [Theory]
    [MemberData(nameof(Broken))]
    public void ABrokenFieldIsRefused(string reason, Action<Notification> breakIt)
    {
        var notification = Valid();
        breakIt(notification);

        Assert.True(NotificationRules.Check(notification) is not null, reason);
    }

    private static Notification Valid() => new()
    {
        Id = "n-0001",
        Priority = NotificationPriority.Normal,
        Source = "Mosaic",
        Title = "Test notification",
        Text = "",
        OccurredAt = 1793903400,
    };
}
