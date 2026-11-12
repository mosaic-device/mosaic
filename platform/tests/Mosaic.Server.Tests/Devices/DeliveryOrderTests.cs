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
    private readonly FakeTimeProvider _fake = new(new DateTimeOffset(2026, 11, 4, 12, 0, 0, TimeSpan.Zero));
    private readonly Clock _clock;
    private readonly DeviceConnections _connections = new();
    private readonly byte[] _secretHash = TestDevice.Create().SecretHash;
    private readonly DeviceConnection _connection;
    private readonly NotificationDelivery _delivery;

    public DeliveryOrderTests()
    {
        _clock = new Clock(_fake);
        _connection = _connections.Register(_secretHash);
        _connection.MarkReady();
        _delivery = new NotificationDelivery(_connections, _clock, NullLogger<NotificationDelivery>.Instance);
    }

    [Fact]
    public async Task ASignalLeftFromATimedOutAttemptDoesNotSendTheNextOneTwice()
    {
        // A's send signal is still queued when A times out, as when the socket is stuck writing.
        var first = Start();
        _fake.Advance(NotificationDelivery.ReceiptTimeout);
        Assert.Equal(NotificationOutcome.NoAnswer, await first);
        var second = Start();

        // The loop now handles both queued signals, one after the other.
        Assert.Equal(DeviceSignal.Deliver, await _connection.NextSignalAsync(CancellationToken.None));
        var sent = _connection.TakeToSend();
        Assert.Equal(DeviceSignal.Deliver, await _connection.NextSignalAsync(CancellationToken.None));

        Assert.NotNull(sent);
        Assert.Null(_connection.TakeToSend());
        _connection.Resolve(Receipt(sent.Notification.Id));
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
    public async Task ANotificationPastItsDeadlineIsNotSent()
    {
        var expired = Start();

        // The deadline has passed but its timeout hasn't been handled yet, as when the loop is slow
        // to reach the send signal: the caller is about to be told no_answer, so nothing goes out.
        _clock.TimerLag = NotificationDelivery.ReceiptTimeout;
        Assert.Null(_connection.TakeToSend());
        Assert.False(expired.IsCompleted);

        _clock.TimerLag = TimeSpan.Zero;
        _fake.Advance(NotificationDelivery.ReceiptTimeout);
        Assert.Equal(NotificationOutcome.NoAnswer, await expired);
    }

    [Fact]
    public async Task AReceiptCountsOnlyBeforeTheDeadline()
    {
        var onTime = Start();
        var sent = _connection.TakeToSend()!;
        _fake.Advance(NotificationDelivery.ReceiptTimeout - TimeSpan.FromMilliseconds(1));
        _connection.Resolve(Receipt(sent.Notification.Id));
        Assert.Equal(NotificationOutcome.Delivered, await onTime);

        // At the deadline the receipt is too late, even if the timeout hasn't been handled yet.
        var late = Start();
        var lateSent = _connection.TakeToSend()!;
        _clock.TimerLag = NotificationDelivery.ReceiptTimeout;
        _connection.Resolve(Receipt(lateSent.Notification.Id));
        Assert.False(late.IsCompleted);

        _clock.TimerLag = TimeSpan.Zero;
        _fake.Advance(NotificationDelivery.ReceiptTimeout);
        Assert.Equal(NotificationOutcome.NoAnswer, await late);
    }

    [Fact]
    public async Task AReceiptBeforeTheSendIsIgnored()
    {
        // A late receipt for an earlier attempt with the same ID arrives before this one is sent.
        var waiting = Start();
        var id = _connection.Awaiting!.Notification.Id;
        _connection.Resolve(Receipt(id));
        Assert.False(waiting.IsCompleted);

        // It is still sent, and its own receipt settles it.
        var sent = _connection.TakeToSend();
        Assert.NotNull(sent);
        _connection.Resolve(Receipt(id));
        Assert.Equal(NotificationOutcome.Delivered, await waiting);
    }

    [Fact]
    public async Task AWallClockChangeDoesNotMoveTheDeadline()
    {
        // The clock jumps forward an hour two seconds in: the receipt is still on time.
        var forward = Start();
        var sent = _connection.TakeToSend()!;
        _fake.Advance(TimeSpan.FromSeconds(2));
        _clock.WallClockJump = TimeSpan.FromHours(1);
        Assert.Equal(8, _delivery.Start(1, _secretHash, Notification()).RetryAfterSeconds);
        _connection.Resolve(Receipt(sent.Notification.Id));
        Assert.Equal(NotificationOutcome.Delivered, await forward);

        // The clock jumps back an hour: the attempt still times out after ten seconds.
        _clock.WallClockJump = -TimeSpan.FromHours(1);
        var backward = Start();
        var backwardSent = _connection.TakeToSend()!;
        _fake.Advance(NotificationDelivery.ReceiptTimeout);
        Assert.Equal(NotificationOutcome.NoAnswer, await backward);
        _connection.Resolve(Receipt(backwardSent.Notification.Id));
    }

    [Fact]
    public void RetryAfterCountsDownToTheSameDeadline()
    {
        _ = Start();
        _ = _connection.TakeToSend()!;

        _fake.Advance(TimeSpan.FromMilliseconds(3500));
        Assert.Equal(7, _delivery.Start(1, _secretHash, Notification()).RetryAfterSeconds);
        _clock.TimerLag = NotificationDelivery.ReceiptTimeout - TimeSpan.FromMilliseconds(3501);
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
        _connection.Resolve(Receipt(id));
        Assert.Equal(NotificationOutcome.Delivered, await waiting);
    }

    private static NotificationReceipt Receipt(string id) => new() { NotificationId = id, Status = NotificationReceiptStatus.Stored };

    private Notification Notification() => DeviceApi.TestNotification(_clock.GetUtcNow());

    private Task<NotificationOutcome> Start()
    {
        var start = _delivery.Start(1, _secretHash, Notification());
        Assert.Equal(DeliveryStartKind.Started, start.Kind);
        return start.Outcome!;
    }

    /// <summary>
    /// A <see cref="FakeTimeProvider"/> whose wall clock can jump without elapsed time moving, and
    /// whose elapsed time can run ahead of its timers, as when a timeout is due but not yet handled.
    /// </summary>
    private sealed class Clock(FakeTimeProvider fake) : TimeProvider
    {
        public TimeSpan WallClockJump { get; set; }

        public TimeSpan TimerLag { get; set; }

        public override long TimestampFrequency => fake.TimestampFrequency;

        public override DateTimeOffset GetUtcNow() => fake.GetUtcNow() + WallClockJump;

        public override long GetTimestamp() =>
            fake.GetTimestamp() + (TimerLag.Ticks * fake.TimestampFrequency / TimeSpan.TicksPerSecond);

        public override ITimer CreateTimer(TimerCallback callback, object? state, TimeSpan dueTime, TimeSpan period) =>
            fake.CreateTimer(callback, state, dueTime, period);
    }
}
