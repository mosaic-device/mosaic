using System.Net;
using System.Net.WebSockets;
using Google.Protobuf;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Mosaic.Device.V1;
using Mosaic.Server.Data;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

public sealed class DeviceEndpointTests : IDisposable
{
    private const string CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

    // An envelope whose only field is an unknown payload, field 15.
    private static readonly byte[] UnknownPayload = Convert.FromHexString("7A020801");

    private readonly DeviceApiFactory _factory = new();

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task AnUnknownCredentialGetsAPairingCodeAndItsLifetime()
    {
        await using var device = await _factory.ConnectAsync();

        var reply = await device.HandshakeAsync();

        Assert.Equal(PayloadCase.PairingPending, reply.PayloadCase);
        Assert.Equal(8, reply.PairingPending.Code.Length);
        Assert.All(reply.PairingPending.Code, character => Assert.Contains(character, CodeAlphabet));
        Assert.Equal(600u, reply.PairingPending.ExpiresInSeconds);
        Assert.True(_factory.Connections.IsConnected(device.Device.SecretHash));
    }

    [Fact]
    public async Task APairedCredentialIsAuthenticated()
    {
        var identity = TestDevice.Create();
        _factory.Pair(identity);
        await using var device = await _factory.ConnectAsync(identity);

        var reply = await device.HandshakeAsync();

        Assert.Equal(PayloadCase.Paired, reply.PayloadCase);
        Assert.True(_factory.Connections.IsConnected(identity.SecretHash));
        Assert.Equal(0, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task AReconnectBeforeExpiryKeepsTheSameCode()
    {
        var identity = TestDevice.Create();
        string code;
        await using (var first = await _factory.ConnectAsync(identity))
        {
            code = (await first.HandshakeAsync()).PairingPending.Code;
        }

        _factory.Time.Advance(TimeSpan.FromMinutes(2));
        await using var second = await _factory.ConnectAsync(identity);
        var reply = await second.HandshakeAsync();

        Assert.Equal(code, reply.PairingPending.Code);
        Assert.Equal(480u, reply.PairingPending.ExpiresInSeconds);
    }

    [Fact]
    public async Task AReconnectAfterExpiryGetsANewCode()
    {
        var identity = TestDevice.Create();
        string code;
        await using (var first = await _factory.ConnectAsync(identity))
        {
            code = (await first.HandshakeAsync()).PairingPending.Code;
        }

        _factory.Time.Advance(PairingCodes.Lifetime);
        await using var second = await _factory.ConnectAsync(identity);
        var reply = await second.HandshakeAsync();

        Assert.NotEqual(code, reply.PairingPending.Code);
        Assert.Equal(600u, reply.PairingPending.ExpiresInSeconds);
        Assert.Equal(CodeState.Expired, _factory.Codes.Find(code).State);
    }

    [Fact]
    public async Task ACodeIsReplacedAtExpiryWhileConnected()
    {
        await using var device = await _factory.ConnectAsync();
        var first = await device.HandshakeAsync();

        _factory.Time.Advance(PairingCodes.Lifetime);
        var replacement = await device.ReceiveAsync();

        Assert.Equal(PayloadCase.PairingPending, replacement.PayloadCase);
        Assert.NotEqual(first.PairingPending.Code, replacement.PairingPending.Code);
        Assert.Equal(600u, replacement.PairingPending.ExpiresInSeconds);
        Assert.Equal(CodeState.Expired, _factory.Codes.Find(first.PairingPending.Code).State);
        Assert.Equal(CodeState.Live, _factory.Codes.Find(replacement.PairingPending.Code).State);
    }

    [Fact]
    public async Task LiveCodesAreUniqueAndSeparateFromTheSetupCode()
    {
        var codes = new HashSet<string>();
        var devices = new List<FakeDevice>();
        try
        {
            for (var i = 0; i < 10; i++)
            {
                var device = await _factory.ConnectAsync();
                devices.Add(device);
                Assert.True(codes.Add((await device.HandshakeAsync()).PairingPending.Code));
            }
        }
        finally
        {
            foreach (var device in devices)
            {
                await device.DisposeAsync();
            }
        }

        Assert.Equal(10, _factory.Codes.LiveCount);
        Assert.Equal(ClaimOutcome.Unknown, _factory.Claim(DeviceApiFactory.SetupCode).Outcome);
        Assert.Equal(10, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task AClaimedCodeTellsTheWaitingDeviceItIsPaired()
    {
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;

        // Any case, with an optional dash.
        var claim = _factory.Claim($"{code[..4]}-{code[4..]}".ToLowerInvariant());
        Assert.Equal(ClaimOutcome.Paired, claim.Outcome);
        Assert.True(claim.Connected);

        var reply = await device.ReceiveAsync();
        Assert.Equal(PayloadCase.Paired, reply.PayloadCase);
        Assert.Equal(CodeState.Used, _factory.Codes.Find(code).State);

        // Once paired, the code no longer expires into a new one.
        _factory.Time.Advance(PairingCodes.Lifetime * 2);
        await device.SendRawAsync(UnknownPayload);
        Assert.True(_factory.Connections.NotifyRevoked(device.Device.SecretHash));
        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
    }

    [Fact]
    public async Task AClaimCommittedAsTheCodeExpiresIsReportedAsPaired()
    {
        await using var device = await _factory.ConnectAsync();
        await device.HandshakeAsync();
        _factory.Devices.Pair(device.Device.SecretHash);

        // The claim committed but its notification hasn't arrived when the code would expire.
        _factory.Time.Advance(PairingCodes.Lifetime);

        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        Assert.Equal(0, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task ARevokedCredentialIsRejectedAndNeverGetsACode()
    {
        var identity = TestDevice.Create();
        Assert.NotNull(_factory.Devices.Revoke(_factory.Pair(identity)));
        await using var device = await _factory.ConnectAsync(identity);

        await device.SendAsync(identity.Connect());

        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
        Assert.Equal(0, _factory.Codes.LiveCount);
        Assert.Null(_factory.Devices.Pair(identity.SecretHash));
        await WaitUntil(() => !_factory.Connections.IsConnected(identity.SecretHash));
    }

    [Fact]
    public async Task RemovingAConnectedDeviceRejectsAndClosesIt()
    {
        var identity = TestDevice.Create();
        var id = _factory.Pair(identity);
        await using var device = await _factory.ConnectAsync(identity);
        await device.HandshakeAsync();

        _factory.Devices.Revoke(id);
        _factory.Connections.NotifyRevoked(identity.SecretHash);

        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
        await WaitUntil(() => !_factory.Connections.IsConnected(identity.SecretHash));
    }

    public static TheoryData<string, byte[]> MalformedFirstFrames()
    {
        var device = TestDevice.Create();
        DeviceToBackendEnvelope With(Action<DeviceConnect> change)
        {
            var frame = device.Connect();
            change(frame.Connect);
            return frame;
        }

        return new TheoryData<string, byte[]>
        {
            { "secret too short", With(connect => connect.AuthSecret = ByteString.CopyFrom(device.Secret[..31])).ToByteArray() },
            { "secret too long", With(connect => connect.AuthSecret = ByteString.CopyFrom([.. device.Secret, 0])).ToByteArray() },
            { "secret missing", With(connect => connect.AuthSecret = ByteString.Empty).ToByteArray() },
            { "empty envelope", new DeviceToBackendEnvelope().ToByteArray() },
            { "unknown payload", UnknownPayload },
            { "not protobuf", [0xFF, 0xFF, 0xFF] },
        };
    }

    [Theory]
    [MemberData(nameof(MalformedFirstFrames))]
    public async Task AMalformedFirstFrameIsRejectedBeforeAnyAccess(string reason, byte[] frame)
    {
        _ = reason;
        await using var device = await _factory.ConnectAsync();

        await device.SendRawAsync(frame);

        var reply = await device.ReceiveAsync();
        Assert.Equal(PayloadCase.Rejected, reply.PayloadCase);
        Assert.Equal(ConnectionRejectReason.MalformedRequest, reply.Rejected.Reason);
        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await device.ReceiveCloseAsync());
        Assert.Equal(0, _factory.Codes.LiveCount);
        Assert.Equal(0, _factory.Connections.Count);
    }

    [Theory]
    [InlineData(0u)]
    [InlineData(2u)]
    public async Task AnUnsupportedVersionIsRejectedBeforeTheFields(uint version)
    {
        await using var device = await _factory.ConnectAsync();
        var frame = device.Device.Connect(version);
        frame.Connect.AuthSecret = ByteString.CopyFromUtf8("not a secret");

        await device.SendAsync(frame);

        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await device.ExpectRejectedAsync(ConnectionRejectReason.UnsupportedVersion));
        Assert.Equal(0, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task ATextFrameIsClosedWith1003()
    {
        await using var device = await _factory.ConnectAsync();

        await device.SendRawAsync("connect"u8.ToArray(), WebSocketMessageType.Text);

        Assert.Equal(WebSocketCloseStatus.InvalidMessageType, await device.ReceiveCloseAsync());
    }

    [Fact]
    public async Task AFrameOverTheDeviceLimitIsClosedWith1009()
    {
        await using var device = await _factory.ConnectAsync();

        await device.SendRawAsync(new byte[DeviceEndpoint.MaxDeviceFrameBytes + 1]);

        Assert.Equal(WebSocketCloseStatus.MessageTooBig, await device.ReceiveCloseAsync());
    }

    [Fact]
    public async Task NoConnectWithinTheHandshakeDeadlineIsClosedWith1008()
    {
        await using var device = await _factory.ConnectAsync();
        var close = device.ReceiveCloseAsync();

        // The deadline starts when the server accepts, so keep moving the clock until it fires.
        while (!close.IsCompleted)
        {
            _factory.Time.Advance(TimeSpan.FromSeconds(5));
            await Task.WhenAny(close, Task.Delay(50, TestContext.Current.CancellationToken));
        }

        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await close);
    }

    [Fact]
    public async Task APendingDeviceThatSendsAnythingIsRejected()
    {
        await using var device = await _factory.ConnectAsync();
        await device.HandshakeAsync();

        await device.SendRawAsync(UnknownPayload);

        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await device.ExpectRejectedAsync(ConnectionRejectReason.MalformedRequest));
    }

    [Fact]
    public async Task APairedDeviceIgnoresUnknownPayloadsButNotASecondConnect()
    {
        var identity = TestDevice.Create();
        _factory.Pair(identity);
        await using var device = await _factory.ConnectAsync(identity);
        await device.HandshakeAsync();

        await device.SendRawAsync(UnknownPayload);
        await device.SendRawAsync(new DeviceToBackendEnvelope().ToByteArray());
        await device.SendAsync(identity.Connect());

        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await device.ExpectRejectedAsync(ConnectionRejectReason.MalformedRequest));
    }

    [Fact]
    public async Task ANewerConnectionReplacesTheOlderOne()
    {
        var identity = TestDevice.Create();
        _factory.Pair(identity);
        await using var older = await _factory.ConnectAsync(identity);
        await older.HandshakeAsync();

        await using var newer = await _factory.ConnectAsync(identity);
        Assert.Equal(PayloadCase.Paired, (await newer.HandshakeAsync()).PayloadCase);

        Assert.Equal(WebSocketCloseStatus.NormalClosure, await older.ReceiveCloseAsync());
        Assert.True(_factory.Connections.IsConnected(identity.SecretHash));
        Assert.Equal(1, _factory.Connections.Count);
    }

    [Fact]
    public async Task UnpairedDevicesBeyondTheLimitAreToldTheServerIsBusy()
    {
        using var factory = new DeviceApiFactory(settings => settings["Devices:MaxPendingPairings"] = "1");
        await using (var first = await factory.ConnectAsync())
        {
            Assert.Equal(PayloadCase.PairingPending, (await first.HandshakeAsync()).PayloadCase);
        }

        // The first code outlives its connection, so it still holds the only slot.
        await using var second = await factory.ConnectAsync();
        await second.SendAsync(second.Device.Connect());
        Assert.Equal(WebSocketCloseStatus.PolicyViolation, await second.ExpectRejectedAsync(ConnectionRejectReason.ServerBusy));

        // A paired device never needs a slot.
        var paired = TestDevice.Create();
        factory.Pair(paired);
        await using var pairedDevice = await factory.ConnectAsync(paired);
        Assert.Equal(PayloadCase.Paired, (await pairedDevice.HandshakeAsync()).PayloadCase);

        factory.Time.Advance(PairingCodes.Lifetime);
        await using var third = await factory.ConnectAsync();
        Assert.Equal(PayloadCase.PairingPending, (await third.HandshakeAsync()).PayloadCase);
    }

    [Fact]
    public async Task PendingCodesAreLostOnRestart()
    {
        var identity = TestDevice.Create();
        string code;
        using (var before = new DeviceApiFactory(directory: _factory.Directory))
        {
            await using var device = await before.ConnectAsync(identity);
            code = (await device.HandshakeAsync()).PairingPending.Code;
        }

        using var after = new DeviceApiFactory(directory: _factory.Directory);
        await using var reconnected = await after.ConnectAsync(identity);
        var reply = await reconnected.HandshakeAsync();

        Assert.NotEqual(code, reply.PairingPending.Code);
        Assert.Equal(600u, reply.PairingPending.ExpiresInSeconds);
    }

    [Fact]
    public async Task ADeviceThatStopsAnsweringPingsIsDisconnected()
    {
        using var factory = new DeviceApiFactory(settings =>
        {
            settings["Devices:KeepAliveInterval"] = "00:00:00.100";
            settings["Devices:KeepAliveTimeout"] = "00:00:00.200";
        });
        var silent = TestDevice.Create();
        var responsive = TestDevice.Create();
        factory.Pair(silent);
        factory.Pair(responsive);
        await using var silentDevice = await factory.ConnectAsync(silent);
        await using var responsiveDevice = await factory.ConnectAsync(responsive);
        await silentDevice.HandshakeAsync();
        await responsiveDevice.HandshakeAsync();

        // A client answers pings only while it is reading, so this one keeps answering and the
        // other, which never reads again, looks dead.
        var reading = responsiveDevice.ReceiveCloseAsync();

        await WaitUntil(() => !factory.Connections.IsConnected(silent.SecretHash));
        Assert.True(factory.Connections.IsConnected(responsive.SecretHash));
        Assert.False(reading.IsCompleted);
    }

    [Fact]
    public async Task OnlyTheSecretHashIsStoredAndNeitherSecretNorCodeIsLogged()
    {
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        Assert.Equal(ClaimOutcome.Paired, _factory.Claim(code).Outcome);
        await device.ReceiveAsync();

        using var connection = new MosaicDatabase(Path.Combine(_factory.Directory, "mosaic.db")).OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT secret_hash FROM devices";
        using var reader = command.ExecuteReader();
        Assert.True(reader.Read());
        Assert.Equal(device.Device.SecretHash, (byte[])reader[0]);
        Assert.False(reader.Read());

        Assert.NotEmpty(_factory.Logs);
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(Convert.ToHexString(device.Device.Secret), StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(Convert.ToBase64String(device.Device.Secret), StringComparison.Ordinal));
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(code, StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task ShutdownClosesEveryOpenConnection()
    {
        using var factory = new DeviceApiFactory();
        var paired = TestDevice.Create();
        factory.Pair(paired);
        await using var pairedDevice = await factory.ConnectAsync(paired);
        await using var pendingDevice = await factory.ConnectAsync();
        Assert.Equal(PayloadCase.Paired, (await pairedDevice.HandshakeAsync()).PayloadCase);
        Assert.Equal(PayloadCase.PairingPending, (await pendingDevice.HandshakeAsync()).PayloadCase);

        // Connections that came and went before share the same shutdown signal.
        var other = TestDevice.Create();
        factory.Pair(other);
        for (var i = 0; i < 3; i++)
        {
            await using var brief = await factory.ConnectAsync(other);
            await brief.HandshakeAsync();
        }

        var lifetime = factory.Services.GetRequiredService<IHostApplicationLifetime>();
        var closes = new[] { pairedDevice.ReceiveCloseAsync(), pendingDevice.ReceiveCloseAsync() };
        lifetime.StopApplication();

        Assert.All(await Task.WhenAll(closes), status => Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, status));
    }

    [Fact]
    public async Task ABrowserCannotOpenADeviceConnection()
    {
        using var client = _factory.CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Get, DeviceEndpoint.Path);
        request.Headers.Add("Origin", "https://mosaic.test");

        using var withOrigin = await client.SendAsync(request, TestContext.Current.CancellationToken);
        using var plain = await client.GetAsync(DeviceEndpoint.Path, TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.Forbidden, withOrigin.StatusCode);
        Assert.Equal(HttpStatusCode.BadRequest, plain.StatusCode);

        using var browser = new ClientWebSocket();
        browser.Options.SetRequestHeader("Origin", "https://mosaic.test");
        await Assert.ThrowsAsync<WebSocketException>(() => browser.ConnectAsync(_factory.DeviceUri, TestContext.Current.CancellationToken));
        Assert.Equal(0, _factory.Connections.Count);
    }

    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (!condition())
        {
            Assert.True(DateTime.UtcNow < deadline, "Timed out waiting for the condition.");
            await Task.Delay(25, TestContext.Current.CancellationToken);
        }
    }
}
