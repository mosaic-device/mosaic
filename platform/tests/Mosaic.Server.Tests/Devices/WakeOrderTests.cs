using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// The order an open device connection handles ready work in. A real socket can't reliably have
/// a frame and a signal ready at the same moment, so the choice is checked here directly.
/// </summary>
public sealed class WakeOrderTests
{
    [Theory]
    // stopping, signal, expiry -> handled first. A frame may be ready in every case.
    [InlineData(true, true, true, Wake.Stopping)]
    [InlineData(true, false, false, Wake.Stopping)]
    [InlineData(false, true, true, Wake.Signal)]
    [InlineData(false, true, false, Wake.Signal)]
    [InlineData(false, false, true, Wake.Expiry)]
    [InlineData(false, false, false, Wake.Frame)]
    public void FramesComeLast(bool stopping, bool signal, bool expiry, Wake expected) =>
        Assert.Equal(expected, WakeOrder.Next(stopping, signal, expiry));
}
