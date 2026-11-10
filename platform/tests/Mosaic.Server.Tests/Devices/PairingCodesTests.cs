using Microsoft.Extensions.Time.Testing;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>Interleavings of <see cref="PairingCodes"/> that the endpoint tests can't time.</summary>
public sealed class PairingCodesTests
{
    private readonly FakeTimeProvider _time = new(new DateTimeOffset(2026, 11, 4, 12, 0, 0, TimeSpan.Zero));

    [Fact]
    public void ACodeThatExpiresWhileItsClaimCommitsIsStillMarkedUsed()
    {
        var codes = new PairingCodes(_time, new DeviceConnectionOptions());
        var code = codes.Issue(TestDevice.Create().SecretHash)!.Code;
        Assert.Equal(CodeState.Live, codes.Find(code).State);

        // The claim accepted the code; it runs out while the pairing is being saved.
        _time.Advance(PairingCodes.Lifetime);
        codes.MarkUsed(code);

        Assert.Equal(CodeState.Used, codes.Find(code).State);
        Assert.Equal(0, codes.LiveCount);
        _time.Advance(PairingCodes.Retention - TimeSpan.FromSeconds(1));
        Assert.Equal(CodeState.Used, codes.Find(code).State);
    }

    [Fact]
    public void MarkingACodeThatWasNeverLiveDoesNothing()
    {
        var codes = new PairingCodes(_time, new DeviceConnectionOptions());

        codes.MarkUsed("ABCDEFGH");

        Assert.Equal(CodeState.Unknown, codes.Find("ABCDEFGH").State);
        Assert.Equal(0, codes.RememberedCount);
    }
}
