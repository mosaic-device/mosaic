using System.Globalization;
using System.Net;
using System.Net.WebSockets;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// <c>POST /api/devices/{id}/test-notification</c> (Admin API Contract, section 5) and the delivery
/// path under it (Notification Delivery Protocol), against a real server and fake devices.
/// </summary>
public sealed class DeviceNotificationTests : IDisposable
{
    private readonly DeviceApiFactory _factory = new();

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task AConnectedDeviceGetsTheTestNotificationAndConfirmsIt()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);
        _factory.Time.Advance(TimeSpan.FromMilliseconds(1500));
        var arrived = _factory.Time.GetUtcNow().ToUnixTimeSeconds();

        var send = SendAsync(id, token);
        var notification = await ReceiveNotificationAsync(device);

        Assert.Matches("^test-[0-9a-f]{16}$", notification.Id);
        Assert.Equal(NotificationPriority.Normal, notification.Priority);
        Assert.Equal("Mosaic", notification.Source);
        Assert.Equal("Test notification", notification.Title);
        Assert.Equal("Sent from the admin app.", notification.Text);
        Assert.Equal(arrived, notification.OccurredAt);
        Assert.False(send.IsCompleted);

        await ConfirmAsync(device, notification.Id, NotificationReceiptStatus.Stored);
        var (status, body) = await send;
        Assert.Equal(HttpStatusCode.OK, status);
        Assert.Equal("""{"outcome":"delivered"}""", body.GetRawText());

        // Each send gets a new ID.
        var again = SendAsync(id, token);
        var second = await ReceiveNotificationAsync(device);
        Assert.NotEqual(notification.Id, second.Id);
        await ConfirmAsync(device, second.Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await again).Body));
    }

    [Theory]
    [InlineData(NotificationReceiptStatus.Stored, "delivered")]
    [InlineData(NotificationReceiptStatus.Duplicate, "delivered")]
    // A device must never send these, but it evidently got the notification (section 3).
    [InlineData(NotificationReceiptStatus.Unspecified, "delivered")]
    [InlineData((NotificationReceiptStatus)99, "delivered")]
    [InlineData(NotificationReceiptStatus.Invalid, "rejected")]
    public async Task TheReceiptDecidesTheOutcome(NotificationReceiptStatus receipt, string expected)
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        var notification = await ReceiveNotificationAsync(device);
        await ConfirmAsync(device, notification.Id, receipt);

        Assert.Equal(expected, Outcome((await send).Body));
        var logged = _factory.Logs.Any(line => line.Contains($"rejected notification {notification.Id} as invalid", StringComparison.Ordinal));
        Assert.Equal(receipt == NotificationReceiptStatus.Invalid, logged);

        // The connection stays up either way.
        var next = SendAsync(id, token);
        await ConfirmAsync(device, (await ReceiveNotificationAsync(device)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await next).Body));
    }

    [Fact]
    public async Task AnOfflineDeviceIsReportedOfflineAndSentNothing()
    {
        var token = await _factory.SignInAsync();
        var identity = TestDevice.Create();
        var id = _factory.Pair(identity);

        var (status, body) = await SendAsync(id, token);

        Assert.Equal(HttpStatusCode.OK, status);
        Assert.Equal("offline", Outcome(body));

        // Nothing was waiting for it: once connected, it gets only its paired frame and then the next send.
        await using var device = await _factory.ConnectAsync(identity);
        Assert.Equal(PayloadCase.Paired, (await device.HandshakeAsync()).PayloadCase);
        var send = SendAsync(id, token);
        await ConfirmAsync(device, (await ReceiveNotificationAsync(device)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await send).Body));
    }

    [Fact]
    public async Task AConnectionThatHasNotBeenToldItIsPairedIsOffline()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        await device.HandshakeAsync();

        // The claim is saved but its signal hasn't reached the connection, which still shows its code.
        var id = _factory.Pair(device.Device);
        Assert.Equal("offline", Outcome((await SendAsync(id, token)).Body));

        // When the code runs out the connection reads the claim and is told it is paired.
        _factory.Time.Advance(PairingCodes.Lifetime);
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        var send = SendAsync(id, token);
        await ConfirmAsync(device, (await ReceiveNotificationAsync(device)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await send).Body));
    }

    [Fact]
    public async Task ADeviceThatNeverConfirmsGetsNoAnswerAndKeepsItsConnection()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        var first = await ReceiveNotificationAsync(device);
        _factory.Time.Advance(NotificationDelivery.ReceiptTimeout - TimeSpan.FromSeconds(1));
        await Task.Delay(100, TestContext.Current.CancellationToken);
        Assert.False(send.IsCompleted);
        _factory.Time.Advance(TimeSpan.FromSeconds(1));

        Assert.Equal("no_answer", Outcome((await send).Body));
        Assert.True(_factory.Connections.IsConnected(device.Device.SecretHash));

        // The same socket takes the next one, and the late receipt for the first is ignored.
        var next = SendAsync(id, token);
        var second = await ReceiveNotificationAsync(device);
        await ConfirmAsync(device, first.Id, NotificationReceiptStatus.Stored);
        await Task.Delay(200, TestContext.Current.CancellationToken);
        Assert.False(next.IsCompleted);
        await ConfirmAsync(device, second.Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await next).Body));
    }

    [Fact]
    public async Task ASecondSendWhileOneIsWaitingIs429()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);
        var (other, otherId) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        var notification = await ReceiveNotificationAsync(device);
        _factory.Time.Advance(TimeSpan.FromMilliseconds(3500));

        using var client = _factory.CreateClient();
        using var busy = await client.SendAsync(Post(id, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.TooManyRequests, busy.StatusCode);
        Assert.Equal("7", Assert.Single(busy.Headers.GetValues("Retry-After")));
        var problem = JsonDocument.Parse(await busy.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement;
        Assert.Equal("too_many_requests", problem.GetProperty("code").GetString());

        // Another device isn't held up.
        var otherSend = SendAsync(otherId, token);
        await ConfirmAsync(other, (await ReceiveNotificationAsync(other)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await otherSend).Body));

        // Its receipt still settles the first send.
        await ConfirmAsync(device, notification.Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await send).Body));
        await other.DisposeAsync();
        await device.DisposeAsync();
    }

    [Fact]
    public async Task ADeviceThatDisconnectsWhileWaitingGetsNoAnswer()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        await ReceiveNotificationAsync(device);
        await device.DisposeAsync();

        // Without the clock moving: the dropped connection settles it.
        Assert.Equal("no_answer", Outcome((await send).Body));
        await WaitUntil(() => !_factory.Connections.IsConnected(device.Device.SecretHash));
        Assert.Equal("offline", Outcome((await SendAsync(id, token)).Body));
    }

    [Fact]
    public async Task AReplacedConnectionGetsNoAnswerAndTheNewOneIsNotSentTheOldNotification()
    {
        var token = await _factory.SignInAsync();
        var (old, id) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        var first = await ReceiveNotificationAsync(old);
        await using var device = await _factory.ConnectAsync(old.Device);
        Assert.Equal(PayloadCase.Paired, (await device.HandshakeAsync()).PayloadCase);

        Assert.Equal("no_answer", Outcome((await send).Body));
        var next = SendAsync(id, token);
        var second = await ReceiveNotificationAsync(device);
        Assert.NotEqual(first.Id, second.Id);
        await ConfirmAsync(device, second.Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await next).Body));
        await old.DisposeAsync();
    }

    [Fact]
    public async Task RemovingADeviceWhileWaitingGetsNoAnswer()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);

        var send = SendAsync(id, token);
        await ReceiveNotificationAsync(device);
        using (var client = _factory.CreateClient())
        {
            using var removal = DeviceApiFactory.Json(HttpMethod.Delete, $"{DeviceApi.ListPath}/{id}", "", token);
            removal.Content = null;
            using var removed = await client.SendAsync(removal, TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NoContent, removed.StatusCode);
        }

        Assert.Equal(WebSocketCloseStatus.NormalClosure, await device.ExpectRejectedAsync(ConnectionRejectReason.CredentialRevoked));
        Assert.Equal("no_answer", Outcome((await send).Body));
        AssertDeviceNotFound(await SendAsync(id, token));
        await device.DisposeAsync();
    }

    [Fact]
    public async Task ACallerThatGivesUpDoesNotFreeTheDeviceEarly()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);
        using var client = _factory.CreateClient();
        using var giveUp = new CancellationTokenSource();

        var abandoned = client.SendAsync(Post(id, token), giveUp.Token);
        var notification = await ReceiveNotificationAsync(device);
        await giveUp.CancelAsync();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => abandoned);

        // The notification is still waiting for its receipt.
        using (var busy = await client.SendAsync(Post(id, token), TestContext.Current.CancellationToken))
        {
            Assert.Equal(HttpStatusCode.TooManyRequests, busy.StatusCode);
        }

        // Its receipt frees the device, though nobody is waiting for the answer any more.
        await ConfirmAsync(device, notification.Id, NotificationReceiptStatus.Stored);
        var next = await SendOnceFreeAsync(id, token);
        await ConfirmAsync(device, (await ReceiveNotificationAsync(device)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await next).Body));
        await device.DisposeAsync();
    }

    [Fact]
    public async Task OriginThenSessionThenTheDevice()
    {
        var token = await _factory.SignInAsync();
        var (device, id) = await ClaimConnectedAsync(token);
        using var client = _factory.CreateClient();

        Assert.Equal(HttpStatusCode.Forbidden, (await SendAsync(id, token: null, origin: "https://evil.example")).Status);
        Assert.Equal(HttpStatusCode.Forbidden, (await SendAsync(id, token, origin: null)).Status);
        var (anonymous, problem) = await SendAsync(id, token: null);
        Assert.Equal(HttpStatusCode.Unauthorized, anonymous);
        Assert.Equal("unauthenticated", problem.GetProperty("code").GetString());
        AssertDeviceNotFound(await SendAsync(99, token));

        // An ID that isn't a whole number matches no route: an empty 404.
        using (var request = DeviceApiFactory.Json(HttpMethod.Post, "/api/devices/abc/test-notification", "", token))
        {
            request.Content = null;
            using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
            Assert.Empty(await response.Content.ReadAsByteArrayAsync(TestContext.Current.CancellationToken));
        }

        // None of those reached the device.
        var send = SendAsync(id, token);
        await ConfirmAsync(device, (await ReceiveNotificationAsync(device)).Id, NotificationReceiptStatus.Stored);
        Assert.Equal("delivered", Outcome((await send).Body));
        await device.DisposeAsync();
    }

    [Fact]
    public void ANotificationThatBreaksAFieldRuleIsNeverSent()
    {
        var delivery = new NotificationDelivery(new DeviceConnections(), TimeProvider.System, NullLogger<NotificationDelivery>.Instance);
        var notification = DeviceApi.TestNotification(DateTimeOffset.UnixEpoch);

        var refused = Assert.Throws<ArgumentException>(() => delivery.Start(1, new byte[32], notification));
        Assert.Contains("after 1970", refused.Message, StringComparison.Ordinal);
    }

    private static void AssertDeviceNotFound((HttpStatusCode Status, JsonElement Body) response)
    {
        Assert.Equal(HttpStatusCode.NotFound, response.Status);
        Assert.Equal("device_not_found", response.Body.GetProperty("code").GetString());
    }

    private static string? Outcome(JsonElement body) => body.GetProperty("outcome").GetString();

    private static HttpRequestMessage Post(long id, string? token, string? origin = DeviceApiFactory.Origin)
    {
        var request = DeviceApiFactory.Json(HttpMethod.Post, $"{DeviceApi.ListPath}/{id}/test-notification", "", token, origin);
        request.Content = null;
        return request;
    }

    private static async Task<Notification> ReceiveNotificationAsync(FakeDevice device)
    {
        var frame = await device.ReceiveAsync();
        Assert.Equal(PayloadCase.Notification, frame.PayloadCase);
        return frame.Notification;
    }

    private static Task ConfirmAsync(FakeDevice device, string notificationId, NotificationReceiptStatus status) =>
        device.SendAsync(new DeviceToBackendEnvelope
        {
            NotificationReceipt = new NotificationReceipt { NotificationId = notificationId, Status = status },
        });

    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (!condition())
        {
            Assert.True(DateTime.UtcNow < deadline, "Timed out waiting for the condition.");
            await Task.Delay(25, TestContext.Current.CancellationToken);
        }
    }

    /// <summary>
    /// Starts a send, retrying while the device is still busy with a receipt that just arrived.
    /// Returns the send that started, which is waiting for its own receipt.
    /// </summary>
    private async Task<Task<(HttpStatusCode Status, JsonElement Body)>> SendOnceFreeAsync(long id, string token)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (true)
        {
            var send = SendAsync(id, token);
            if (await Task.WhenAny(send, Task.Delay(300, TestContext.Current.CancellationToken)) != send)
            {
                return send;
            }

            Assert.Equal(HttpStatusCode.TooManyRequests, (await send).Status);
            Assert.True(DateTime.UtcNow < deadline, "The device was never freed.");
            await Task.Delay(25, TestContext.Current.CancellationToken);
        }
    }

    private async Task<(HttpStatusCode Status, JsonElement Body)> SendAsync(long id, string? token, string? origin = DeviceApiFactory.Origin)
    {
        using var client = _factory.CreateClient();
        using var request = Post(id, token, origin);
        using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
        var text = await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken);
        return (response.StatusCode, text.Length == 0 ? default : JsonDocument.Parse(text).RootElement.Clone());
    }

    /// <summary>Connects a new device, claims its code over HTTP, and leaves it connected and paired.</summary>
    private async Task<(FakeDevice Device, long Id)> ClaimConnectedAsync(string token)
    {
        var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        using var client = _factory.CreateClient();
        using var response = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var id = JsonDocument.Parse(await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken))
            .RootElement.GetProperty("device").GetProperty("id").GetInt64();
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        return (device, id);
    }
}
