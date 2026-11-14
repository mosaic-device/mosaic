using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>What happened to one notification (Notification Delivery Protocol, section 4).</summary>
public enum NotificationOutcome
{
    /// <summary>The device holds the notification with this ID: receipt <c>STORED</c> or <c>DUPLICATE</c>.</summary>
    Delivered,

    /// <summary>The device isn't connected or hasn't been sent <c>paired</c> yet, so nothing was sent.</summary>
    Offline,

    /// <summary>No receipt within <see cref="NotificationDelivery.ReceiptTimeout"/>, or the connection ended first.</summary>
    NoAnswer,

    /// <summary>The device refused it as invalid: receipt <c>INVALID</c>. A server bug, so it is logged.</summary>
    Rejected,
}

/// <summary>One notification sent to one device. Its outcome is set exactly once.</summary>
/// <remarks>
/// Its deadline is kept on <see cref="TimeProvider.GetTimestamp"/>, which a wall-clock change
/// doesn't move, so the timeout, the receipt cutoff and <c>Retry-After</c> all agree on it.
/// </remarks>
public sealed class Delivery
{
    private readonly TaskCompletionSource<NotificationOutcome> _outcome = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TimeProvider _time;
    private readonly long _started;
    private int _taken;

    internal Delivery(long deviceId, Notification notification, TimeProvider time)
    {
        DeviceId = deviceId;
        Notification = notification;
        _time = time;
        _started = time.GetTimestamp();
    }

    public long DeviceId { get; }

    public Notification Notification { get; }

    /// <summary>
    /// How long until it gets <see cref="NotificationOutcome.NoAnswer"/> if no receipt has come;
    /// zero or less once the deadline has passed.
    /// </summary>
    internal TimeSpan TimeLeft => NotificationDelivery.ReceiptTimeout - _time.GetElapsedTime(_started);

    internal Task<NotificationOutcome> Outcome => _outcome.Task;

    /// <summary>Whether the connection's loop has taken it for sending.</summary>
    internal bool IsTaken => Volatile.Read(ref _taken) != 0;

    internal bool TryComplete(NotificationOutcome outcome) => _outcome.TrySetResult(outcome);

    /// <summary>
    /// True for the first caller only. A connection can hold more than one send signal when an
    /// earlier attempt timed out before its signal was handled; only one of them may send this.
    /// </summary>
    internal bool TryTakeForSending() => Interlocked.Exchange(ref _taken, 1) == 0;
}

public enum DeliveryStartKind
{
    /// <summary>Handed to the device's connection; wait for <see cref="DeliveryStart.Outcome"/>.</summary>
    Started,

    /// <summary>Nothing was sent: the device is offline.</summary>
    Offline,

    /// <summary>Another notification to this device is still waiting for its receipt.</summary>
    Busy,
}

/// <summary>
/// The result of <see cref="NotificationDelivery.Start"/>. <see cref="Outcome"/> is set when it
/// started, and completes once the device is free for the next notification.
/// <see cref="RetryAfterSeconds"/> is the whole seconds left on the waiting one when busy.
/// </summary>
public sealed record DeliveryStart(DeliveryStartKind Kind, Task<NotificationOutcome>? Outcome = null, int RetryAfterSeconds = 0);

/// <summary>
/// Sends notifications to devices and learns whether they arrived (Notification Delivery Protocol).
/// One live attempt per notification: it never queues or resends, since that is the producer's
/// job. The test notification uses it today, and M4's notifications will use it with their own
/// payloads.
/// </summary>
/// <remarks>
/// At most one notification per device waits for its receipt. The device's connection loop does
/// the sending, so it stays the only writer to the socket and still handles shutdown, removal and
/// replacement first. An attempt settles on its receipt, when its connection ends, or at its
/// deadline, whether or not anyone is still waiting for the answer.
/// </remarks>
public sealed partial class NotificationDelivery(DeviceConnections connections, TimeProvider time, ILogger<NotificationDelivery> logger)
{
    public static readonly TimeSpan ReceiptTimeout = TimeSpan.FromSeconds(10);

    private readonly Lock _lock = new();
    private readonly Dictionary<long, Delivery> _inFlight = [];

    /// <summary>
    /// Sends <paramref name="notification"/> to the device with this ID and credential, if it is
    /// connected and paired and nothing else to it is waiting for a receipt.
    /// </summary>
    /// <exception cref="ArgumentException">The notification breaks a field rule (<see cref="NotificationRules"/>).</exception>
    public DeliveryStart Start(long deviceId, byte[] secretHash, Notification notification)
    {
        // Checked, sent and matched as it is now: a caller changing its object later can't
        // slip past the rules or break the receipt match.
        notification = notification.Clone();
        if (NotificationRules.Check(notification) is { } problem)
        {
            throw new ArgumentException(problem, nameof(notification));
        }

        Delivery delivery;
        DeviceConnection? connection;
        lock (_lock)
        {
            if (_inFlight.TryGetValue(deviceId, out var waiting))
            {
                var left = (int)Math.Ceiling(waiting.TimeLeft.TotalSeconds);
                return new DeliveryStart(DeliveryStartKind.Busy, RetryAfterSeconds: Math.Clamp(left, 1, (int)ReceiptTimeout.TotalSeconds));
            }

            delivery = new Delivery(deviceId, notification, time);
            connection = connections.Find(secretHash);
            if (connection is null || !connection.TryDeliver(delivery))
            {
                return new DeliveryStart(DeliveryStartKind.Offline);
            }

            _inFlight.Add(deviceId, delivery);
        }

        return new DeliveryStart(DeliveryStartKind.Started, SettleAsync(delivery, connection));
    }

    /// <summary>
    /// Waits for the receipt or the deadline, then frees the connection and the device before
    /// reporting the outcome, so a caller that sends again straight away isn't told it is busy.
    /// </summary>
    private async Task<NotificationOutcome> SettleAsync(Delivery delivery, DeviceConnection connection)
    {
        using (var deadline = new CancellationTokenSource())
        {
            // Times out at the same deadline Retry-After counts down to and receipts are checked against.
            var left = delivery.TimeLeft;
            await Task.WhenAny(delivery.Outcome, Task.Delay(left > TimeSpan.Zero ? left : TimeSpan.Zero, time, deadline.Token));
            delivery.TryComplete(NotificationOutcome.NoAnswer);
            await deadline.CancelAsync();
        }

        connection.Release(delivery);
        lock (_lock)
        {
            if (_inFlight.TryGetValue(delivery.DeviceId, out var current) && ReferenceEquals(current, delivery))
            {
                _inFlight.Remove(delivery.DeviceId);
            }
        }

        var outcome = await delivery.Outcome;
        if (outcome == NotificationOutcome.Rejected)
        {
            LogRejected(delivery.DeviceId, delivery.Notification.Id);
        }

        return outcome;
    }

    // Notification IDs aren't secret; their content is never logged.
    [LoggerMessage(Level = LogLevel.Warning, Message = "Device {DeviceId} rejected notification {NotificationId} as invalid")]
    private partial void LogRejected(long deviceId, string notificationId);
}
