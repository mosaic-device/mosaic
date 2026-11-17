using System.Net;
using System.Net.WebSockets;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// The limit on device connections that haven't finished their handshake, and the seat a paired
/// device gets by presenting its credential at the upgrade.
/// </summary>
public sealed class HandshakeBudgetTests : IDisposable
{
    private const int Limit = 3;

    private readonly DeviceApiFactory _factory = new(settings => settings["Devices:MaxHandshakes"] = Limit.ToString());

    public enum Ending
    {
        Timeout,
        Rejection,
        DeviceClose,
        DeviceAbort,
        PairingPending,
        Paired,
    }

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task SilentConnectionsPastTheLimitAreRefusedBeforeTheUpgrade()
    {
        var silent = await OpenSilentAsync(Limit);

        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync());
        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync());
        Assert.Equal(Limit, _factory.Handshakes.Open);
        Assert.All(silent, device => Assert.Equal(WebSocketState.Open, device.Socket.State));

        // Logged once for the flood, not once per refusal.
        Assert.Single(_factory.Logs, line => line.Contains("Refusing device connections", StringComparison.Ordinal));
        await DisposeAllAsync(silent);
    }

    [Fact]
    public async Task APairedDeviceReconnectsWhileAnonymousClientsHoldTheLimit()
    {
        var silent = await OpenSilentAsync(Limit);
        var device = TestDevice.Create();
        _factory.Pair(device);

        await using (var paired = await _factory.ConnectAsync(device, presentCredential: true))
        {
            Assert.Equal(PayloadCase.Paired, (await paired.HandshakeAsync()).PayloadCase);

            // Its seat is free again once it has its reply; the connection stays up.
            await WaitUntil(() => _factory.Handshakes.Open == Limit);
            Assert.True(_factory.Connections.IsConnected(device.SecretHash));
        }

        await DisposeAllAsync(silent);
    }

    [Fact]
    public async Task OnlyAPairedCredentialAtTheUpgradeGetsASeat()
    {
        var silent = await OpenSilentAsync(Limit);
        var paired = TestDevice.Create();
        _factory.Pair(paired);
        var revoked = TestDevice.Create();
        _factory.Devices.Revoke(_factory.Pair(revoked));

        // Without the header a paired device is anonymous, and unknown or revoked secrets get
        // no seat, so made-up secrets can't take places held for devices.
        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync());
        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync(Hex(TestDevice.Create())));
        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync(Hex(revoked)));
        await using var withoutHeader = await ConnectAfterAsync(silent[0], paired, presentCredential: false);
        Assert.Equal(PayloadCase.Paired, (await withoutHeader.HandshakeAsync()).PayloadCase);
        await DisposeAllAsync(silent[1..]);
    }

    [Fact]
    public async Task RepeatedConnectionsWithOneCredentialShareItsSeat()
    {
        var device = TestDevice.Create();
        _factory.Pair(device);

        // With room, a second handshake with the same credential takes a shared place.
        var first = await _factory.ConnectAsync(device, presentCredential: true);
        var second = await _factory.ConnectAsync(device, presentCredential: true);
        Assert.Equal(2, _factory.Handshakes.Open);
        await DisposeAllAsync([second]);
        await WaitUntil(() => _factory.Handshakes.Open == 1);

        // With no room, the seat holds one handshake at a time, so repeats can't fill the server.
        var silent = await OpenSilentAsync(Limit);
        Assert.Equal(HttpStatusCode.ServiceUnavailable, await _factory.RefusedUpgradeAsync(Hex(device)));

        // Once the earlier handshake ends, the device gets back in.
        Assert.Equal(Limit + 1, _factory.Handshakes.Open);
        await DisposeAllAsync([first]);
        await WaitUntil(() => _factory.Handshakes.Open == Limit);
        await using var retry = await _factory.ConnectAsync(device, presentCredential: true);
        Assert.Equal(PayloadCase.Paired, (await retry.HandshakeAsync()).PayloadCase);
        await DisposeAllAsync(silent);
    }

    [Theory]
    [InlineData(Ending.Timeout)]
    [InlineData(Ending.Rejection)]
    [InlineData(Ending.DeviceClose)]
    [InlineData(Ending.DeviceAbort)]
    [InlineData(Ending.PairingPending)]
    [InlineData(Ending.Paired)]
    public async Task AHandshakeGivesItsPlaceBackHoweverItEnds(Ending ending)
    {
        var device = TestDevice.Create();
        if (ending == Ending.Paired)
        {
            _factory.Pair(device);
        }

        var connection = await _factory.ConnectAsync(device, presentCredential: ending == Ending.Paired);
        Assert.Equal(1, _factory.Handshakes.Open);

        switch (ending)
        {
            case Ending.Timeout:
                var close = connection.ReceiveCloseAsync();
                while (!close.IsCompleted)
                {
                    _factory.Time.Advance(TimeSpan.FromSeconds(5));
                    await Task.WhenAny(close, Task.Delay(50, TestContext.Current.CancellationToken));
                }

                Assert.Equal(WebSocketCloseStatus.PolicyViolation, await close);
                break;
            case Ending.Rejection:
                await connection.SendAsync(device.Connect(version: 2));
                await connection.ExpectRejectedAsync(ConnectionRejectReason.UnsupportedVersion);
                break;
            case Ending.DeviceClose:
                await connection.Socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, TestContext.Current.CancellationToken);
                break;
            case Ending.DeviceAbort:
                connection.Socket.Abort();
                break;
            case Ending.PairingPending:
                Assert.Equal(PayloadCase.PairingPending, (await connection.HandshakeAsync()).PayloadCase);
                break;
            case Ending.Paired:
                Assert.Equal(PayloadCase.Paired, (await connection.HandshakeAsync()).PayloadCase);
                break;
        }

        await WaitUntil(() => _factory.Handshakes.Open == 0);

        // The places are all usable again.
        var after = await OpenSilentAsync(Limit);
        await DisposeAllAsync([connection, .. after]);
    }

    [Fact]
    public async Task TheConnectFrameMustCarryTheSecretFromTheUpgrade()
    {
        var presented = TestDevice.Create();
        _factory.Pair(presented);
        var other = TestDevice.Create();
        _factory.Pair(other);

        await using var device = await _factory.ConnectAsync(presented, presentCredential: true);
        await device.SendAsync(other.Connect());

        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await device.ExpectRejectedAsync(ConnectionRejectReason.MalformedRequest));
        Assert.False(_factory.Connections.IsConnected(other.SecretHash));
        Assert.Contains(_factory.Logs, line => line.Contains("named different credentials", StringComparison.Ordinal));
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(Hex(presented), StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task AnUnknownCredentialAtTheUpgradePairsAsUsual()
    {
        var unknown = TestDevice.Create();

        // Hex digits in either case.
        using var socket = new ClientWebSocket();
        socket.Options.SetRequestHeader(DeviceEndpoint.CredentialHeader, Convert.ToHexString(unknown.Secret));
        await socket.ConnectAsync(_factory.DeviceUri, TestContext.Current.CancellationToken);
        await using var device = new FakeDevice(socket, unknown);

        Assert.Equal(PayloadCase.PairingPending, (await device.HandshakeAsync()).PayloadCase);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not hex at all, but exactly sixty-four characters long, see.....")]
    [InlineData("00112233445566778899aabbccddeeff00112233445566778899aabbccddee")]
    [InlineData("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00")]
    [InlineData("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff, 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff")]
    public async Task AMalformedUpgradeCredentialGets400(string header)
    {
        Assert.Equal(HttpStatusCode.BadRequest, await _factory.RefusedUpgradeAsync(header));
        Assert.Equal(0, _factory.Handshakes.Open);
    }

    private static string Hex(TestDevice device) => Convert.ToHexStringLower(device.Secret);

    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (!condition())
        {
            Assert.True(DateTime.UtcNow < deadline, "Timed out waiting for the condition.");
            await Task.Delay(25, TestContext.Current.CancellationToken);
        }
    }

    private static async Task DisposeAllAsync(IEnumerable<FakeDevice> devices)
    {
        foreach (var device in devices)
        {
            await device.DisposeAsync();
        }
    }

    /// <summary>Opens <paramref name="count"/> connections that never send a frame.</summary>
    private async Task<FakeDevice[]> OpenSilentAsync(int count)
    {
        var before = _factory.Handshakes.Open;
        var devices = new FakeDevice[count];
        for (var i = 0; i < count; i++)
        {
            devices[i] = await _factory.ConnectAsync();
        }

        Assert.Equal(before + count, _factory.Handshakes.Open);
        return devices;
    }

    /// <summary>Ends <paramref name="silent"/>, waits for its place to be free, then connects <paramref name="device"/>.</summary>
    private async Task<FakeDevice> ConnectAfterAsync(FakeDevice silent, TestDevice device, bool presentCredential)
    {
        var open = _factory.Handshakes.Open;
        await silent.DisposeAsync();
        await WaitUntil(() => _factory.Handshakes.Open == open - 1);
        return await _factory.ConnectAsync(device, presentCredential);
    }
}
