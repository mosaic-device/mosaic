namespace Mosaic.Server.Devices;

/// <summary>What an open device connection handles next.</summary>
public enum Wake
{
    /// <summary>The server is shutting down; close the connection.</summary>
    Stopping,

    /// <summary>Another part of the server signalled the connection (paired, revoked or replaced).</summary>
    Signal,

    /// <summary>The device's pairing code ran out.</summary>
    Expiry,

    /// <summary>A frame from the device.</summary>
    Frame,
}

/// <summary>
/// Chooses which of several ready things a device connection handles first. Shutdown and signals
/// come before the device's own frames, so a device that keeps sending can't hold off being told
/// it was removed, being replaced, or the server stopping.
/// </summary>
public static class WakeOrder
{
    /// <summary>
    /// Called once at least one thing is ready. A frame is handled only when nothing else is ready,
    /// and the loop comes back for the rest.
    /// </summary>
    public static Wake Next(bool stopping, bool signal, bool expiry) =>
        stopping ? Wake.Stopping
        : signal ? Wake.Signal
        : expiry ? Wake.Expiry
        : Wake.Frame;
}
