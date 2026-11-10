using System.Net;
using System.Net.WebSockets;
using System.Text.Json;
using Mosaic.Device.V1;
using Mosaic.Server.Data;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary><c>GET /api/devices</c> and <c>DELETE /api/devices/{id}</c> (Admin API Contract, section 5).</summary>
public sealed class DeviceManagementTests : IDisposable
{
    // An envelope whose only field is an unknown payload, field 15. A paired device may send it.
    private static readonly byte[] UnknownPayload = Convert.FromHexString("7A020801");

    private readonly DeviceApiFactory _factory = new();

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task WithNoDevicesTheListIsEmpty()
    {
        var token = await _factory.SignInAsync();
        using var client = _factory.CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Get, DeviceApi.ListPath);
        request.Headers.Add("Cookie", $"mosaic_session={token}");

        using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("no-store", response.Headers.CacheControl?.ToString());
        Assert.Equal("""{"devices":[]}""", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken));
    }

    [Fact]
    public async Task TheListShowsPairedDevicesInOrderWithWhetherTheyAreConnected()
    {
        var token = await _factory.SignInAsync();
        await using var online = await ClaimConnectedAsync(token);
        var offline = TestDevice.Create();
        await ClaimAsync(await ShowCodeAndLeaveAsync(offline), token);
        var removed = await ClaimAsync(await ShowCodeAndLeaveAsync(TestDevice.Create()), token);
        Assert.Equal(HttpStatusCode.NoContent, (await DeleteAsync(removed, token)).Status);

        // A device still showing its code isn't paired, so it isn't listed.
        await using var pending = await _factory.ConnectAsync();
        await pending.HandshakeAsync();

        var devices = await ListAsync(token);

        Assert.Equal([1L, 2L], devices.Select(device => device.GetProperty("id").GetInt64()));
        Assert.Equal([true, false], devices.Select(device => device.GetProperty("connected").GetBoolean()));
        Assert.All(devices, device =>
        {
            Assert.Equal(["id", "pairedAt", "connected"], device.EnumerateObject().Select(property => property.Name));
            Assert.Equal("2026-11-04T12:00:00Z", device.GetProperty("pairedAt").GetString());
        });

        // Connection status is read on every request.
        await online.DisposeAsync();
        await using var reconnected = await _factory.ConnectAsync(offline);
        Assert.Equal(PayloadCase.Paired, (await reconnected.HandshakeAsync()).PayloadCase);
        await WaitUntil(() => !_factory.Connections.IsConnected(online.Device.SecretHash));
        devices = await ListAsync(token);
        Assert.Equal([false, true], devices.Select(device => device.GetProperty("connected").GetBoolean()));
    }

    [Fact]
    public async Task RemovingAConnectedDeviceTellsItThenClosesIt()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        var id = await ClaimAsync(code, token);
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);

        var (status, body) = await DeleteAsync(id, token);

        Assert.Equal(HttpStatusCode.NoContent, status);
        Assert.Equal(JsonValueKind.Undefined, body.ValueKind);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
        Assert.Empty(await ListAsync(token));

        // Removing it again is the same as an ID that never existed.
        var (again, problem) = await DeleteAsync(id, token);
        AssertDeviceNotFound(again, problem);

        // Its code is still remembered, and now says it was used.
        using var client = _factory.CreateClient();
        using var retry = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.Conflict, retry.StatusCode);
    }

    [Fact]
    public async Task ADeviceRemovedWhileOfflineIsRejectedWhenItReconnects()
    {
        var token = await _factory.SignInAsync();
        var identity = TestDevice.Create();
        var id = await ClaimAsync(await ShowCodeAndLeaveAsync(identity), token);

        Assert.Equal(HttpStatusCode.NoContent, (await DeleteAsync(id, token)).Status);

        await using (var device = await _factory.ConnectAsync(identity))
        {
            await device.SendAsync(identity.Connect());
            Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
        }

        Assert.Equal(0, _factory.Codes.LiveCount);

        // The device makes a new secret, shows a new code, and pairs as a new device.
        var fresh = TestDevice.Create();
        Assert.Equal(id + 1, await ClaimAsync(await ShowCodeAndLeaveAsync(fresh), token));
        Assert.Equal([id + 1], (await ListAsync(token)).Select(device => device.GetProperty("id").GetInt64()));
    }

    [Fact]
    public async Task ARevocationSurvivesARestart()
    {
        var identity = TestDevice.Create();
        using (var before = new DeviceApiFactory(directory: _factory.Directory))
        {
            var token = await before.SignInAsync();
            await using var device = await before.ConnectAsync(identity);
            var code = (await device.HandshakeAsync()).PairingPending.Code;
            var id = Assert.IsType<long>(before.Claim(code).Device?.Id);
            using var client = before.CreateClient();
            using var removal = await client.SendAsync(Delete(id, token), TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NoContent, removal.StatusCode);
        }

        using var after = new DeviceApiFactory(directory: _factory.Directory);
        await using var reconnected = await after.ConnectAsync(identity);
        await reconnected.SendAsync(identity.Connect());
        Assert.Equal(WebSocketCloseStatus.NormalClosure, await reconnected.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
    }

    [Fact]
    public async Task AReconnectRacingRemovalNeverKeepsAccess()
    {
        var token = await _factory.SignInAsync();
        for (var round = 0; round < 10; round++)
        {
            // The old socket is still open when the device reconnects, as after a network blip.
            var old = await ClaimConnectedAsync(token);
            var id = (await ListAsync(token)).Last().GetProperty("id").GetInt64();

            var removal = DeleteAsync(id, token);
            await using var device = await _factory.ConnectAsync(old.Device);
            var reply = await device.HandshakeAsync();
            Assert.Equal(HttpStatusCode.NoContent, (await removal).Status);

            // Either removal came first and the handshake reads it, or the new connection was
            // admitted first and is then told.
            if (reply.PayloadCase == PayloadCase.Paired)
            {
                reply = await device.ReceiveAsync();
            }

            Assert.Equal(PayloadCase.Rejected, reply.PayloadCase);
            Assert.Equal(ConnectionRejectReason.CredentialRevoked, reply.Rejected.Reason);
            Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ReceiveCloseAsync());

            // The old socket is closed too: told it was removed, or replaced by the new one.
            await AssertClosedAsync(old);
            await old.DisposeAsync();
        }

        Assert.Empty(await ListAsync(token));
        await WaitUntil(() => _factory.Connections.Count == 0);
    }

    [Fact]
    public async Task ADeviceThatKeepsSendingIsStillToldItWasRemoved()
    {
        var token = await _factory.SignInAsync();
        await using var device = await ClaimConnectedAsync(token);

        // Frames a paired device may send keep arriving, so one is always waiting to be read.
        using var stop = new CancellationTokenSource();
        var flood = Task.Run(async () =>
        {
            try
            {
                while (!stop.IsCancellationRequested)
                {
                    await device.Socket.SendAsync(UnknownPayload, WebSocketMessageType.Binary, endOfMessage: true, stop.Token);
                }
            }
            catch (Exception exception) when (exception is OperationCanceledException or WebSocketException or ObjectDisposedException)
            {
                // Stopped, or the server closed the connection.
            }
        }, TestContext.Current.CancellationToken);
        await Task.Delay(100, TestContext.Current.CancellationToken);

        Assert.Equal(HttpStatusCode.NoContent, (await DeleteAsync(1, token)).Status);

        var reply = await device.ReceiveAsync();
        await stop.CancelAsync();
        await flood;
        Assert.Equal(PayloadCase.Rejected, reply.PayloadCase);
        Assert.Equal(ConnectionRejectReason.CredentialRevoked, reply.Rejected.Reason);
    }

    [Fact]
    public async Task OfConcurrentRemovalsOnlyOneSucceeds()
    {
        var token = await _factory.SignInAsync();
        await using var device = await ClaimConnectedAsync(token);

        var removals = await Task.WhenAll(Enumerable.Range(0, 5).Select(_ => DeleteAsync(1, token)));

        Assert.Single(removals, removal => removal.Status == HttpStatusCode.NoContent);
        Assert.All(removals.Where(removal => removal.Status != HttpStatusCode.NoContent), removal => AssertDeviceNotFound(removal.Status, removal.Body));
        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
    }

    [Fact]
    public async Task AFailedRevocationLeavesTheDevicePairedAndUntold()
    {
        var token = await _factory.SignInAsync();
        await using var device = await ClaimConnectedAsync(token);
        Execute("CREATE TRIGGER fail_revoke BEFORE UPDATE ON devices BEGIN SELECT RAISE(ABORT, 'disk full'); END");

        var (status, body) = await DeleteAsync(1, token);

        Assert.Equal(HttpStatusCode.InternalServerError, status);
        Assert.Equal("internal_error", body.GetProperty("code").GetString());
        Assert.Equal([1L], (await ListAsync(token)).Select(listed => listed.GetProperty("id").GetInt64()));
        Assert.True(_factory.Connections.IsConnected(device.Device.SecretHash));

        // The next thing the device hears is the removal that does succeed, not an earlier one.
        Execute("DROP TRIGGER fail_revoke");
        await device.SendRawAsync(UnknownPayload);
        Assert.Equal(HttpStatusCode.NoContent, (await DeleteAsync(1, token)).Status);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
    }

    [Fact]
    public async Task OriginThenSessionThenTheDevice()
    {
        var token = await _factory.SignInAsync();
        await using var device = await ClaimConnectedAsync(token);
        using var client = _factory.CreateClient();

        Assert.Equal(HttpStatusCode.Forbidden, (await DeleteAsync(1, token: null, origin: "https://evil.example")).Status);
        Assert.Equal(HttpStatusCode.Forbidden, (await DeleteAsync(1, token, origin: null)).Status);
        var (anonymous, problem) = await DeleteAsync(1, token: null);
        Assert.Equal(HttpStatusCode.Unauthorized, anonymous);
        Assert.Equal("unauthenticated", problem.GetProperty("code").GetString());
        Assert.Equal(HttpStatusCode.Unauthorized, (await DeleteAsync(1, token: "not-a-session")).Status);
        Assert.Equal(HttpStatusCode.Unauthorized, (await client.GetAsync(DeviceApi.ListPath, TestContext.Current.CancellationToken)).StatusCode);

        var (missing, notFound) = await DeleteAsync(99, token);
        AssertDeviceNotFound(missing, notFound);

        // An ID that isn't a whole number matches no route: an empty 404, after the Origin check.
        foreach (var path in new[] { "/api/devices/abc", "/api/devices/1.5", "/api/devices/99999999999999999999" })
        {
            using var request = DeviceApiFactory.Json(HttpMethod.Delete, path, "", token);
            request.Content = null;
            using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
            Assert.Empty(await response.Content.ReadAsByteArrayAsync(TestContext.Current.CancellationToken));
        }

        using var crossSite = DeviceApiFactory.Json(HttpMethod.Delete, "/api/devices/abc", "", token, origin: "https://evil.example");
        using var crossSiteResponse = await client.SendAsync(crossSite, TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.Forbidden, crossSiteResponse.StatusCode);

        // Nothing was removed.
        Assert.Single(await ListAsync(token));
        Assert.True(_factory.Connections.IsConnected(device.Device.SecretHash));
    }

    [Fact]
    public async Task LogoutAndSessionExpiryLeaveDevicesAlone()
    {
        var token = await _factory.SignInAsync();
        await using var device = await ClaimConnectedAsync(token);
        using var client = _factory.CreateClient();
        using (var logout = DeviceApiFactory.Json(HttpMethod.Post, "/api/auth/logout", "", token))
        {
            logout.Content = null;
            using var response = await client.SendAsync(logout, TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NoContent, response.StatusCode);
        }

        // Every session has expired, but the device keeps its connection and its access.
        var login = await LoginAsync(client);
        _factory.Time.Advance(TimeSpan.FromDays(31));
        Assert.Equal(HttpStatusCode.Unauthorized, (await DeleteAsync(1, login)).Status);
        await device.SendRawAsync(UnknownPayload);
        Assert.True(_factory.Connections.IsConnected(device.Device.SecretHash));
        await device.DisposeAsync();
        await WaitUntil(() => !_factory.Connections.IsConnected(device.Device.SecretHash));
        await using var reconnected = await _factory.ConnectAsync(device.Device);
        Assert.Equal(PayloadCase.Paired, (await reconnected.HandshakeAsync()).PayloadCase);

        var devices = await ListAsync(await LoginAsync(client));
        Assert.Equal([1L], devices.Select(listed => listed.GetProperty("id").GetInt64()));
        Assert.True(Assert.Single(devices).GetProperty("connected").GetBoolean());
    }

    private static void AssertDeviceNotFound(HttpStatusCode status, JsonElement body)
    {
        Assert.Equal(HttpStatusCode.NotFound, status);
        Assert.Equal(404, body.GetProperty("status").GetInt32());
        Assert.Equal("device_not_found", body.GetProperty("code").GetString());
        Assert.Equal("That device isn't paired with this server.", body.GetProperty("title").GetString());
    }

    /// <summary>Expects the socket to close, after a <c>CREDENTIAL_REVOKED</c> or straight away.</summary>
    private static async Task AssertClosedAsync(FakeDevice device)
    {
        var buffer = new byte[4096];
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        var result = await device.Socket.ReceiveAsync(buffer, timeout.Token);
        if (result.MessageType == WebSocketMessageType.Binary)
        {
            var reply = BackendToDeviceEnvelope.Parser.ParseFrom(buffer, 0, result.Count);
            Assert.Equal(ConnectionRejectReason.CredentialRevoked, reply.Rejected.Reason);
            result = await device.Socket.ReceiveAsync(buffer, timeout.Token);
        }

        Assert.Equal(WebSocketMessageType.Close, result.MessageType);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, device.Socket.CloseStatus);
    }

    private static HttpRequestMessage Delete(long id, string? token, string? origin = DeviceApiFactory.Origin)
    {
        var request = DeviceApiFactory.Json(HttpMethod.Delete, $"{DeviceApi.ListPath}/{id}", "", token, origin);
        request.Content = null;
        return request;
    }

    private static async Task<string> LoginAsync(HttpClient client)
    {
        using var request = DeviceApiFactory.Json(HttpMethod.Post, "/api/auth/login", """{"username":"Bridger","password":"mosaic-desk-2026"}""");
        using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var cookie = Assert.Single(response.Headers.GetValues("Set-Cookie"), value => value.StartsWith("mosaic_session=", StringComparison.Ordinal));
        return cookie["mosaic_session=".Length..cookie.IndexOf(';', StringComparison.Ordinal)];
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

    private async Task<JsonElement[]> ListAsync(string token)
    {
        using var client = _factory.CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Get, DeviceApi.ListPath);
        request.Headers.Add("Cookie", $"mosaic_session={token}");
        using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement.Clone();
        Assert.Single(body.EnumerateObject());
        return [.. body.GetProperty("devices").EnumerateArray()];
    }

    private async Task<(HttpStatusCode Status, JsonElement Body)> DeleteAsync(long id, string? token, string? origin = DeviceApiFactory.Origin)
    {
        using var client = _factory.CreateClient();
        using var request = Delete(id, token, origin);
        using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
        var text = await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken);
        return (response.StatusCode, text.Length == 0 ? default : JsonDocument.Parse(text).RootElement.Clone());
    }

    /// <summary>Claims <paramref name="code"/> over HTTP and returns the device's ID.</summary>
    private async Task<long> ClaimAsync(string code, string token)
    {
        using var client = _factory.CreateClient();
        using var response = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement;
        return body.GetProperty("device").GetProperty("id").GetInt64();
    }

    /// <summary>Connects a new device, claims its code, and leaves it connected and paired.</summary>
    private async Task<FakeDevice> ClaimConnectedAsync(string token)
    {
        var device = await _factory.ConnectAsync();
        await ClaimAsync((await device.HandshakeAsync()).PairingPending.Code, token);
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        return device;
    }

    /// <summary>Connects as <paramref name="identity"/>, reads its code, and disconnects.</summary>
    private async Task<string> ShowCodeAndLeaveAsync(TestDevice identity)
    {
        string code;
        await using (var device = await _factory.ConnectAsync(identity))
        {
            code = (await device.HandshakeAsync()).PairingPending.Code;
        }

        await WaitUntil(() => !_factory.Connections.IsConnected(identity.SecretHash));
        return code;
    }

    private void Execute(string sql)
    {
        using var connection = new MosaicDatabase(Path.Combine(_factory.Directory, "mosaic.db")).OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }
}
