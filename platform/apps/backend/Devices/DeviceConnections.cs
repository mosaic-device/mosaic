using System.Threading.Channels;

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
}

/// <summary>One open device connection that has sent a valid <c>connect</c>.</summary>
public sealed class DeviceConnection
{
    private readonly Channel<DeviceSignal> _signals = Channel.CreateUnbounded<DeviceSignal>(
        new UnboundedChannelOptions { SingleReader = true });

    internal DeviceConnection(string key) => Key = key;

    internal string Key { get; }

    internal ValueTask<DeviceSignal> NextSignalAsync(CancellationToken cancellationToken) =>
        _signals.Reader.ReadAsync(cancellationToken);

    internal void Signal(DeviceSignal signal) => _signals.Writer.TryWrite(signal);
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

        previous?.Signal(DeviceSignal.Replaced);
        return connection;
    }

    internal void Unregister(DeviceConnection connection)
    {
        lock (_lock)
        {
            if (_connections.TryGetValue(connection.Key, out var current) && ReferenceEquals(current, connection))
            {
                _connections.Remove(connection.Key);
            }
        }
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
