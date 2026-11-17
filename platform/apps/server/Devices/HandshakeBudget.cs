namespace Mosaic.Server.Devices;

/// <summary>
/// Bounds the device connections that haven't finished their handshake. Anyone can open one, and
/// until its first frame arrives the server can't tell a device from a flood, so these share
/// <see cref="DeviceConnectionOptions.MaxHandshakes"/>. A device that proves a paired credential
/// at the upgrade also has a seat of its own, one handshake at a time per credential, so it can
/// reconnect while anonymous clients hold every shared place. Paired devices are added only by
/// the admin, which bounds the seats.
/// </summary>
public sealed class HandshakeBudget(DeviceConnectionOptions options)
{
    private readonly Lock _lock = new();
    private readonly HashSet<string> _seats = new(StringComparer.Ordinal);
    private int _shared;
    private bool _full;

    /// <summary>Handshakes holding a shared place or a paired credential's seat.</summary>
    public int Open
    {
        get
        {
            lock (_lock)
            {
                return _shared + _seats.Count;
            }
        }
    }

    /// <summary>
    /// Takes a place for one handshake: the seat of <paramref name="pairedSecretHash"/> when it is
    /// given and free, otherwise a shared place. Null when neither is free; then
    /// <paramref name="firstRefusal"/> says whether this is the first refusal since there was room.
    /// </summary>
    public HandshakeSlot? TryEnter(byte[]? pairedSecretHash, out bool firstRefusal)
    {
        firstRefusal = false;
        lock (_lock)
        {
            if (pairedSecretHash is not null)
            {
                var seat = Convert.ToHexString(pairedSecretHash);
                if (_seats.Add(seat))
                {
                    return new HandshakeSlot(this, seat);
                }
            }

            if (_shared < options.MaxHandshakes)
            {
                _shared++;
                _full = false;
                return new HandshakeSlot(this, null);
            }

            firstRefusal = !_full;
            _full = true;
            return null;
        }
    }

    internal void Release(string? seat)
    {
        lock (_lock)
        {
            if (seat is null)
            {
                _shared--;
            }
            else
            {
                _seats.Remove(seat);
            }
        }
    }
}

/// <summary>
/// One handshake's place in the <see cref="HandshakeBudget"/>. Released once the handshake
/// completes or the connection ends, whichever comes first; releasing again does nothing.
/// </summary>
public sealed class HandshakeSlot : IDisposable
{
    private readonly HandshakeBudget _budget;
    private readonly string? _seat;
    private int _released;

    internal HandshakeSlot(HandshakeBudget budget, string? seat)
    {
        _budget = budget;
        _seat = seat;
    }

    /// <summary>Whether this is a paired credential's own seat rather than a shared place.</summary>
    public bool IsSeat => _seat is not null;

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _released, 1) == 0)
        {
            _budget.Release(_seat);
        }
    }
}
