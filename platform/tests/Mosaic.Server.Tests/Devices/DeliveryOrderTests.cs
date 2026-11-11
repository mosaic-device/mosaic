using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Time.Testing;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// Orderings of a connection and its notifications that a real socket can't be made to hit on
/// demand, driven through the same methods the connection loop calls.
/// </summary>
public sealed class DeliveryOrderTests
{
    private readonly FakeTimeProvider _time = new(new DateTimeOffset(2026, 11, 4, 12, 0, 0, TimeSpan.Zero));
    private readonly DeviceConnections _connections = new();
    private readonly byte[] _secretHash = TestDevice.Create().SecretHash;
    private readonly DeviceConnection _connection;
    private readonly NotificationDelivery _delivery;

    public DeliveryOrderTests()
    {
        _connection = _connections.Register(_secretHash);
        _connection.MarkReady();
        _delivery = new NotificationDelivery(_connections, _time, NullLogger<NotificationDelivery>.Instance);
    }

    [Fact]
    public async Task ASignalLeftFromATimedOutAttemptDoesNotSendTheNextOneTwice()
    {
        // A's send signal is still queued when A times out, as when the socket is stuck writing.
        var first = Start();
        _time.Advance(NotificationDelivery.ReceiptTimeout);
        Assert.Equal(NotificationOutcome.NoAnswer, await first);
        var second = Start();

        // The loop now handles both queued signals, one after the other.
        Assert.Equal(DeviceSignal.Deliver, await _connection.NextSignalAsync(CancellationToken.None));
        var sent = _connection.TakeToSend();
        Assert.Equal(DeviceSignal.Deliver, await _connection.NextSignalAsync(CancellationToken.None));

        Assert.NotNull(sent);
        Assert.Null(_connection.TakeToSend());
        _connection.Resolve(Receipt(sent.Notification.Id), _time.GetUtcNow());
        Assert.Equal(NotificationOutcome.Delivered, await second);
    }

    [Fact]
    public async Task RemovalStopsANotificationQueuedBeforeIt()
    {
        var waiting = Start();

        // Removal is signalled before the loop reaches the queued send.
        Assert.True(_connections.NotifyRevoked(_secretHash));

        Assert.Equal(NotificationOutcome.NoAnswer, await waiting);
        Assert.Equal(DeviceSignal.Deliver, await _connection.NextSignalAsync(CancellationToken.None));
        Assert.Null(_connection.TakeToSend());
        Assert.Equal(DeviceSignal.Revoked, await _connection.NextSignalAsync(CancellationToken.None));
        Assert.Equal(DeliveryStartKind.Offline, _delivery.Start(1, _secretHash, Notification()).Kind);
    }

    [Fact]
    public async Task AReceiptCountsOnlyBeforeTheDeadline()
    {
        var onTime = Start();
        var sent = _connection.TakeToSend()!;
        _time.Advance(NotificationDelivery.ReceiptTimeout - TimeSpan.FromMilliseconds(1));
        _connection.Resolve(Receipt(sent.Notification.Id), _time.GetUtcNow());
        Assert.Equal(NotificationOutcome.Delivered, await onTime);

        // At the deadline the receipt is too late, even if the timeout hasn't been handled yet.
        var late = Start();
        var lateSent = _connection.TakeToSend()!;
        var deadline = lateSent.Deadline;
        _connection.Resolve(Receipt(lateSent.Notification.Id), deadline);
        Assert.False(late.IsCompleted);
        _time.SetUtcNow(deadline);
        Assert.Equal(NotificationOutcome.NoAnswer, await late);
    }

    [Fact]
    public void RetryAfterCountsDownToTheSameDeadline()
    {
        _ = Start();
        var sent = _connection.TakeToSend()!;

        _time.Advance(TimeSpan.FromMilliseconds(3500));
        Assert.Equal(7, _delivery.Start(1, _secretHash, Notification()).RetryAfterSeconds);
        _time.SetUtcNow(sent.Deadline - TimeSpan.FromMilliseconds(1));
        Assert.Equal(1, _delivery.Start(1, _secretHash, Notification()).RetryAfterSeconds);
    }

    [Fact]
    public async Task TheNotificationIsCopiedWhenItStarts()
    {
        var notification = Notification();
        var id = notification.Id;
        var waiting = _delivery.Start(1, _secretHash, notification).Outcome!;

        // The caller changes its object after starting, which must not change what is sent or matched.
        notification.Id = "changed";
        notification.Title = "";

        var sent = _connection.TakeToSend()!;
        Assert.Equal(id, sent.Notification.Id);
        Assert.Equal("Test notification", sent.Notification.Title);
        _connection.Resolve(Receipt(id), _time.GetUtcNow());
        Assert.Equal(NotificationOutcome.Delivered, await waiting);
    }

    private static NotificationReceipt Receipt(string id) => new() { NotificationId = id, Status = NotificationReceiptStatus.Stored };

    private Notification Notification() => DeviceApi.TestNotification(_time.GetUtcNow());

    private Task<NotificationOutcome> Start()
    {
        var start = _delivery.Start(1, _secretHash, Notification());
        Assert.Equal(DeliveryStartKind.Started, start.Kind);
        return start.Outcome!;
    }
}
