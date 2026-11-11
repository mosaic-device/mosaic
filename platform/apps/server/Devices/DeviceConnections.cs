using System.Threading.Channels;
using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>Something another part of the server needs a device connection to act on.</summary>
public enum DeviceSignal
{
    /// <summary>An admin claimed this credential and ownership is committed; tell a waiting device.</summary>
    Paired,

    /// <summary>The admin removed this device; send <c>CREDENTIAL_REVOKED</c> and close.</summary>
    Revoked,

    /// <summary>A newer connection with the same credential took over; close this one.</summary>
    Replaced,

    /// <summary>Send the connection's <see cref="DeviceConnection.Awaiting"/> notification.</summary>
    Deliver,
}

/// <summary>
/// One open device connection that has sent a valid <c>connect</c>. It can take a notification
/// once the server has sent it <c>paired</c>, and holds at most one waiting for its receipt.
/// </summary>
public sealed class DeviceConnection
{
    private readonly Channel<DeviceSignal> _signals = Channel.CreateUnbounded<DeviceSignal>(
        new UnboundedChannelOptions { SingleReader = true });

    private readonly Lock _lock = new();
    private bool _ready;
    private bool _closed;
    private Delivery? _awaiting;

    internal DeviceConnection(string key) => Key = key;

    internal string Key { get; }

    /// <summary>The notification sent, or about to be, whose receipt this connection is waiting for.</summary>
    internal Delivery? Awaiting
    {
        get
        {
            lock (_lock)
            {
                return _awaiting;
            }
        }
    }

    internal ValueTask<DeviceSignal> NextSignalAsync(CancellationToken cancellationToken) =>
        _signals.Reader.ReadAsync(cancellationToken);

    internal void Signal(DeviceSignal signal) => _signals.Writer.TryWrite(signal);

    /// <summary>The server has sent this connection <c>paired</c>, so it may now get notifications.</summary>
    internal void MarkReady()
    {
        lock (_lock)
        {
            _ready = true;
        }
    }

    /// <summary>
    /// Hands <paramref name="delivery"/> to the connection's loop, which sends it. False when the
    /// connection isn't paired yet, has ended, or is already waiting for another receipt.
    /// </summary>
    internal bool TryDeliver(Delivery delivery)
    {
        lock (_lock)
        {
            if (_closed || !_ready || _awaiting is not null)
            {
                return false;
            }

            _awaiting = delivery;
        }

        Signal(DeviceSignal.Deliver);
        return true;
    }

    /// <summary>
    /// Settles the waiting notification if <paramref name="receipt"/> is for it. A receipt for
    /// anything else, such as one that arrives after its timeout, is ignored.
    /// </summary>
    internal void Resolve(NotificationReceipt receipt)
    {
        Delivery? delivery;
        lock (_lock)
        {
            delivery = _awaiting;
            if (delivery is null || !string.Equals(delivery.Notification.Id, receipt.NotificationId, StringComparison.Ordinal))
            {
                return;
            }

            _awaiting = null;
        }

        // UNSPECIFIED or an unknown status still means it arrived (section 3).
        delivery.TryComplete(receipt.Status == NotificationReceiptStatus.Invalid
            ? NotificationOutcome.Rejected
            : NotificationOutcome.Delivered);
    }

    /// <summary>The connection has ended. A notification still waiting for its receipt gets no answer.</summary>
    internal void Close()
    {
        Delivery? delivery;
        lock (_lock)
        {
            _closed = true;
            delivery = _awaiting;
            _awaiting = null;
        }

        delivery?.TryComplete(NotificationOutcome.NoAnswer);
    }

    /// <summary>Stops waiting for <paramref name="delivery"/>'s receipt, such as after its deadline.</summary>
    internal void Release(Delivery delivery)
    {
        lock (_lock)
        {
            if (ReferenceEquals(_awaiting, delivery))
            {
                _awaiting = null;
            }
        }
    }
}

/// <summary>
/// The open device connections, at most one per credential: when a credential connects again,
/// the newer connection wins and the older one is closed, since that socket is usually already
/// dead (Device Connection Protocol, section 3, item 8). Claim and removal use this to reach a
/// connected device.
/// </summary>
public sealed class DeviceConnections
{
    private readonly Lock _lock = new();
    private readonly Dictionary<string, DeviceConnection> _connections = new(StringComparer.Ordinal);

    public int Count
    {
        get
        {
            lock (_lock)
            {
                return _connections.Count;
            }
        }
    }

    public bool IsConnected(byte[] secretHash)
    {
        lock (_lock)
        {
            return _connections.ContainsKey(Key(secretHash));
        }
    }

    /// <summary>Tells a connected device with this credential that it is now paired.</summary>
    public bool NotifyPaired(byte[] secretHash) => Signal(secretHash, DeviceSignal.Paired);

    /// <summary>Tells a connected device with this credential that it was removed.</summary>
    public bool NotifyRevoked(byte[] secretHash) => Signal(secretHash, DeviceSignal.Revoked);

    internal DeviceConnection Register(byte[] secretHash)
    {
        var connection = new DeviceConnection(Key(secretHash));
        DeviceConnection? previous;
        lock (_lock)
        {
            _connections.Remove(connection.Key, out previous);
            _connections.Add(connection.Key, connection);
        }

        // The old connection's waiting notification ends now; it is never moved to the new one.
        previous?.Close();
        previous?.Signal(DeviceSignal.Replaced);
        return connection;
    }

    /// <summary>The open connection for this credential, if any.</summary>
    internal DeviceConnection? Find(byte[] secretHash)
    {
        lock (_lock)
        {
            return _connections.GetValueOrDefault(Key(secretHash));
        }
    }

    /// <summary>Removes the connection when it is still the current one, and ends any notification it was waiting on.</summary>
    internal void Unregister(DeviceConnection connection)
    {
        lock (_lock)
        {
            if (_connections.TryGetValue(connection.Key, out var current) && ReferenceEquals(current, connection))
            {
                _connections.Remove(connection.Key);
            }
        }

        connection.Close();
    }

    private bool Signal(byte[] secretHash, DeviceSignal signal)
    {
        DeviceConnection? connection;
        lock (_lock)
        {
            _connections.TryGetValue(Key(secretHash), out connection);
        }

        connection?.Signal(signal);
        return connection is not null;
    }

    private static string Key(byte[] secretHash) => Convert.ToHexString(secretHash);
}
