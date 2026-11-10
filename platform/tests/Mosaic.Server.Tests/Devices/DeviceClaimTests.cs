using System.Globalization;
using System.Net;
using System.Text;
using System.Text.Json;
using Mosaic.Server.Auth;
using Mosaic.Server.Data;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary><c>POST /api/devices/claim</c> (Admin API Contract, section 5), against a real server and fake devices.</summary>
public sealed class DeviceClaimTests : IDisposable
{
    private const string CodeMessage = "Enter the 8-character code shown on the device.";

    private readonly DeviceApiFactory _factory = new();

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task AClaimPairsTheWaitingDeviceAndTellsIt()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        _factory.Time.Advance(TimeSpan.FromMilliseconds(1500));

        // As the admin might type it: lower case, a dash and stray spaces.
        var (status, body) = await ClaimAsync($"  {code[..4]}-{code[4..]}  ".ToLowerInvariant(), token);

        Assert.Equal(HttpStatusCode.OK, status);
        var claimed = body.GetProperty("device");
        Assert.Equal(1, claimed.GetProperty("id").GetInt64());
        Assert.Equal("2026-11-04T12:00:01Z", claimed.GetProperty("pairedAt").GetString());
        Assert.True(claimed.GetProperty("connected").GetBoolean());
        Assert.Equal(3, claimed.EnumerateObject().Count());
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        Assert.Equal(1, DeviceRows());
        Assert.Equal(0, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task ADeviceClaimedWhileOfflineLearnsItOnReconnect()
    {
        var token = await _factory.SignInAsync();
        var identity = TestDevice.Create();
        var code = await ShowCodeAndLeaveAsync(identity);

        var (status, body) = await ClaimAsync(code, token);

        Assert.Equal(HttpStatusCode.OK, status);
        Assert.False(body.GetProperty("device").GetProperty("connected").GetBoolean());
        await using var reconnected = await _factory.ConnectAsync(identity);
        Assert.Equal(PayloadCase.Paired, (await reconnected.HandshakeAsync()).PayloadCase);
    }

    [Fact]
    public async Task ARetryReturnsTheSameDeviceWithItsConnectionNow()
    {
        var token = await _factory.SignInAsync();
        var identity = TestDevice.Create();
        JsonElement first;
        string code;
        await using (var device = await _factory.ConnectAsync(identity))
        {
            code = (await device.HandshakeAsync()).PairingPending.Code;
            (_, first) = await ClaimAsync(code, token);
            Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        }

        await WaitUntil(() => !_factory.Connections.IsConnected(identity.SecretHash));
        _factory.Time.Advance(TimeSpan.FromMinutes(30));

        // The first response was lost; the admin app sends the same claim again.
        var (status, retry) = await ClaimAsync(code, token);

        Assert.Equal(HttpStatusCode.OK, status);
        Assert.Equal(first.GetProperty("device").GetProperty("id").GetInt64(), retry.GetProperty("device").GetProperty("id").GetInt64());
        Assert.Equal(first.GetProperty("device").GetProperty("pairedAt").GetString(), retry.GetProperty("device").GetProperty("pairedAt").GetString());
        Assert.True(first.GetProperty("device").GetProperty("connected").GetBoolean());
        Assert.False(retry.GetProperty("device").GetProperty("connected").GetBoolean());
        Assert.Equal(1, DeviceRows());
    }

    [Fact]
    public async Task ConcurrentClaimsEndWithOneDeviceAndOnePairing()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;

        var claims = await Task.WhenAll(Enumerable.Range(0, 5).Select(_ => ClaimAsync(code, token)));

        Assert.All(claims, claim => Assert.Equal(HttpStatusCode.OK, claim.Status));
        Assert.Single(claims.Select(claim => claim.Body.GetProperty("device").GetProperty("id").GetInt64()).Distinct());
        Assert.Single(claims.Select(claim => claim.Body.GetProperty("device").GetProperty("pairedAt").GetString()).Distinct());
        Assert.Equal(1, DeviceRows());

        // One paired frame: the next thing the device hears is its removal, not a second one.
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
        Assert.True(_factory.Connections.NotifyRevoked(device.Device.SecretHash));
        await device.ExpectRejectedAsync(Mosaic.Device.V1.ConnectionRejectReason.CredentialRevoked);
    }

    [Fact]
    public async Task AClaimRacingAReconnectNeverLeavesTheDeviceWaiting()
    {
        var token = await _factory.SignInAsync();
        for (var round = 0; round < 10; round++)
        {
            var identity = TestDevice.Create();
            var code = await ShowCodeAndLeaveAsync(identity);

            var claim = ClaimAsync(code, token);
            await using var device = await _factory.ConnectAsync(identity);
            var reply = await device.HandshakeAsync();
            Assert.Equal(HttpStatusCode.OK, (await claim).Status);

            // Either the claim committed first, or the device got its old code and is then told.
            if (reply.PayloadCase == PayloadCase.PairingPending)
            {
                Assert.Equal(code, reply.PairingPending.Code);
                Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
            }
            else
            {
                Assert.Equal(PayloadCase.Paired, reply.PayloadCase);
            }
        }

        Assert.Equal(0, _factory.Codes.LiveCount);
    }

    [Fact]
    public async Task AnExpiredCodeIs410AndChangesNothing()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var expired = (await device.HandshakeAsync()).PairingPending.Code;
        _factory.Time.Advance(PairingCodes.Lifetime);
        var replacement = (await device.ReceiveAsync()).PairingPending.Code;

        var (status, body) = await ClaimAsync(expired, token);

        AssertProblem(status, body, HttpStatusCode.Gone, "pairing_code_expired", "That code has expired; enter the one the device shows now.");
        Assert.Equal(0, DeviceRows());
        Assert.Equal(HttpStatusCode.OK, (await ClaimAsync(replacement, token)).Status);
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
    }

    [Fact]
    public async Task UsedAndExpiredCodesAreRememberedForAnHour()
    {
        var token = await _factory.SignInAsync();
        var used = await ShowCodeAndLeaveAsync(TestDevice.Create());
        var expired = await ShowCodeAndLeaveAsync(TestDevice.Create());
        Assert.Equal(HttpStatusCode.OK, (await ClaimAsync(used, token)).Status);

        // The used code counts from its claim; the expired one from its expiry, ten minutes on.
        _factory.Time.Advance(TimeSpan.FromMinutes(59));
        Assert.Equal(HttpStatusCode.OK, (await ClaimAsync(used, token)).Status);
        Assert.Equal(HttpStatusCode.Gone, (await ClaimAsync(expired, token)).Status);
        Assert.Equal(2, _factory.Codes.RememberedCount);

        _factory.Time.Advance(TimeSpan.FromMinutes(1));
        var (status, body) = await ClaimAsync(used, token);
        AssertProblem(status, body, HttpStatusCode.NotFound, "invalid_pairing_code", "No device is showing that code; check it and try again.");
        Assert.Equal(HttpStatusCode.Gone, (await ClaimAsync(expired, token)).Status);

        _factory.Time.Advance(TimeSpan.FromMinutes(10));
        Assert.Equal(HttpStatusCode.NotFound, (await ClaimAsync(expired, token)).Status);
        Assert.Equal(0, _factory.Codes.RememberedCount);
        Assert.Equal(1, DeviceRows());
    }

    [Fact]
    public async Task ACodeWhoseDeviceWasRemovedIs409()
    {
        var token = await _factory.SignInAsync();
        var code = await ShowCodeAndLeaveAsync(TestDevice.Create());
        var (_, claimed) = await ClaimAsync(code, token);
        Assert.True(_factory.Devices.Revoke(claimed.GetProperty("device").GetProperty("id").GetInt64()));

        var (status, body) = await ClaimAsync(code, token);

        AssertProblem(status, body, HttpStatusCode.Conflict, "pairing_code_used", "That code was already used; enter the new code the device shows.");
        Assert.Equal(1, DeviceRows());
    }

    [Fact]
    public async Task AnUnknownCodeIs404AndChangesNothing()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        await device.HandshakeAsync();

        var (status, body) = await ClaimAsync(DeviceApiFactory.SetupCode, token);

        AssertProblem(status, body, HttpStatusCode.NotFound, "invalid_pairing_code", "No device is showing that code; check it and try again.");
        Assert.Equal(0, DeviceRows());
        Assert.Equal(1, _factory.Codes.LiveCount);
    }

    public static TheoryData<string> BadlyFormedCodes() => new()
    {
        "{}",
        """{"code":null}""",
        """{"code":5}""",
        """{"code":["K7QF2M9X"]}""",
        """{"code":""}""",
        """{"code":"K7QF-2M9"}""",
        """{"code":"K7QF-2M9XY"}""",
        """{"code":"K7QF-2M90"}""",
        """{"code":"K7QF-2M9I"}""",
        """{"code":"K7QF 2M9X"}""",
        """{"code":"\tK7QF2M9X"}""",
    };

    [Theory]
    [MemberData(nameof(BadlyFormedCodes))]
    public async Task ABadlyFormedCodeIs400UnderCode(string json)
    {
        var token = await _factory.SignInAsync();

        var (status, body) = await SendAsync(DeviceApiFactory.Json(HttpMethod.Post, DeviceApi.ClaimPath, json, token));

        AssertProblem(status, body, HttpStatusCode.BadRequest, "validation_failed", "Some fields need attention.");
        Assert.Equal(new[] { CodeMessage }, body.GetProperty("errors").GetProperty("code").EnumerateArray().Select(message => message.GetString()));
        Assert.Single(body.GetProperty("errors").EnumerateObject());
    }

    [Theory]
    [InlineData("[]")]
    [InlineData("\"K7QF2M9X\"")]
    [InlineData("null")]
    [InlineData("""{"code":"K7QF2M9X" """)]
    public async Task ABodyThatIsNotAJsonObjectIs400(string json)
    {
        var token = await _factory.SignInAsync();

        var (status, body) = await SendAsync(DeviceApiFactory.Json(HttpMethod.Post, DeviceApi.ClaimPath, json, token));

        AssertProblem(status, body, HttpStatusCode.BadRequest, "validation_failed", "The request body must be a JSON object.");
        Assert.False(body.TryGetProperty("errors", out _));
    }

    [Fact]
    public async Task OriginThenSessionThenContentTypeThenSize()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        var json = JsonSerializer.Serialize(new { code });

        var (crossSite, crossSiteBody) = await SendAsync(PlainText(json, token: null, origin: "https://evil.example"));
        Assert.Equal(HttpStatusCode.Forbidden, crossSite);
        Assert.Equal("forbidden_origin", crossSiteBody.GetProperty("code").GetString());

        var (anonymous, anonymousBody) = await SendAsync(PlainText(json, token: null));
        Assert.Equal(HttpStatusCode.Unauthorized, anonymous);
        Assert.Equal("unauthenticated", anonymousBody.GetProperty("code").GetString());

        Assert.Equal(HttpStatusCode.UnsupportedMediaType, (await SendAsync(PlainText(json, token))).Status);

        var oversized = $$"""{"code":"{{code}}","padding":"{{new string('x', JsonBody.MaxBytes)}}"}""";
        Assert.Equal(HttpStatusCode.RequestEntityTooLarge, (await SendAsync(DeviceApiFactory.Json(HttpMethod.Post, DeviceApi.ClaimPath, oversized, token))).Status);

        // None of those touched the code.
        Assert.Equal(0, DeviceRows());
        Assert.Equal(CodeState.Live, _factory.Codes.Find(code).State);
    }

    [Fact]
    public async Task AStalledBodyHasItsConnectionClosed()
    {
        // The in-memory test server, as in the auth tests: a real socket client keeps waiting to
        // send the rest of the body and never notices the reset.
        using var factory = new AuthApiFactory(settings => settings[AuthRateLimitOptions.BodyReadTimeoutSecondsKey] = "1");
        using var client = factory.CreateApiClient();
        using var setup = await client.SendAsync(AuthApiFactory.Setup(), TestContext.Current.CancellationToken);
        var token = AuthApiFactory.SessionCookie(setup).Value;
        using var stalled = new HttpRequestMessage(HttpMethod.Post, DeviceApi.ClaimPath) { Content = new StalledJsonContent("""{"code":"K7QF2M9X"}""") };
        stalled.Headers.Add("Origin", AuthApiFactory.PublicOrigin);
        stalled.Headers.Add("Cookie", $"mosaic_session={token}");

        var closed = await Record.ExceptionAsync(() =>
            client.SendAsync(stalled, TestContext.Current.CancellationToken).WaitAsync(TimeSpan.FromSeconds(15), TestContext.Current.CancellationToken));

        Assert.NotNull(closed);
        Assert.False(closed is TimeoutException, "The server never closed the stalled connection.");
    }

    [Fact]
    public async Task AFailedSaveLeavesTheCodeClaimable()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        Execute("CREATE TRIGGER fail_pairing BEFORE INSERT ON devices BEGIN SELECT RAISE(ABORT, 'disk full'); END");

        var (failed, failure) = await ClaimAsync(code, token);

        Assert.Equal(HttpStatusCode.InternalServerError, failed);
        Assert.Equal("internal_error", failure.GetProperty("code").GetString());
        Assert.Equal(CodeState.Live, _factory.Codes.Find(code).State);
        Assert.Equal(0, DeviceRows());

        Execute("DROP TRIGGER fail_pairing");
        Assert.Equal(HttpStatusCode.OK, (await ClaimAsync(code, token)).Status);
        Assert.Equal(PayloadCase.Paired, (await device.ReceiveAsync()).PayloadCase);
    }

    [Fact]
    public async Task ARestartForgetsCodesButNotThePairing()
    {
        var identity = TestDevice.Create();
        string token;
        string code;
        using (var before = new DeviceApiFactory(directory: _factory.Directory))
        {
            token = await before.SignInAsync();
            await using var device = await before.ConnectAsync(identity);
            code = (await device.HandshakeAsync()).PairingPending.Code;
            using var client = before.CreateClient();
            using var response = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        }

        using var after = new DeviceApiFactory(directory: _factory.Directory);
        using (var client = after.CreateClient())
        {
            using var retry = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
            Assert.Equal(HttpStatusCode.NotFound, retry.StatusCode);
        }

        await using var reconnected = await after.ConnectAsync(identity);
        Assert.Equal(PayloadCase.Paired, (await reconnected.HandshakeAsync()).PayloadCase);
    }

    [Fact]
    public async Task ClaimedCodesFreeTheirPendingSlot()
    {
        using var factory = new DeviceApiFactory(settings => settings["Devices:MaxPendingPairings"] = "1");
        var token = await factory.SignInAsync();
        await using var first = await factory.ConnectAsync();
        var code = (await first.HandshakeAsync()).PairingPending.Code;
        using var client = factory.CreateClient();
        using var claimed = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, claimed.StatusCode);

        // The used code is remembered, but only live codes hold a slot.
        await using var second = await factory.ConnectAsync();
        Assert.Equal(PayloadCase.PairingPending, (await second.HandshakeAsync()).PayloadCase);
        Assert.Equal(1, factory.Codes.RememberedCount);
    }

    [Fact]
    public async Task ClaimsAreLimitedPerClientWhateverTheirOutcome()
    {
        using var factory = new DeviceApiFactory(settings => settings["Devices:ClaimAttemptsPerMinute"] = "3");
        var token = await factory.SignInAsync();
        using var client = factory.CreateClient();
        await using var device = await factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;

        Assert.Equal(HttpStatusCode.BadRequest, await StatusAsync(client, DeviceApiFactory.ClaimRequest("short", token)));
        Assert.Equal(HttpStatusCode.NotFound, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));
        Assert.Equal(HttpStatusCode.UnsupportedMediaType, await StatusAsync(client, PlainText("{}", token)));

        // Even the right code is refused once the attempts are used up.
        using var limited = await client.SendAsync(DeviceApiFactory.ClaimRequest(code, token), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.TooManyRequests, limited.StatusCode);
        var body = JsonDocument.Parse(await limited.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement;
        Assert.Equal("too_many_requests", body.GetProperty("code").GetString());
        var retryAfter = int.Parse(Assert.Single(limited.Headers.GetValues("Retry-After")), CultureInfo.InvariantCulture);
        Assert.InRange(retryAfter, 1, 60);
        Assert.Equal(CodeState.Live, factory.Codes.Find(code).State);

        // Another client has its own attempts.
        Assert.Equal(HttpStatusCode.OK, await StatusAsync(client, DeviceApiFactory.ClaimRequest(code, token, peer: "203.0.113.7")));
    }

    [Fact]
    public async Task RequestsStoppedBeforeTheLimitDoNotUseItUp()
    {
        using var factory = new DeviceApiFactory(settings => settings["Devices:ClaimAttemptsPerMinute"] = "1");
        var token = await factory.SignInAsync();
        using var client = factory.CreateClient();

        for (var i = 0; i < 5; i++)
        {
            Assert.Equal(HttpStatusCode.Unauthorized, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token: null)));
            Assert.Equal(HttpStatusCode.Unauthorized, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token: "not-a-session")));
            Assert.Equal(HttpStatusCode.Forbidden, await StatusAsync(client, PlainText("{}", token, origin: "https://evil.example")));
        }

        Assert.Equal(HttpStatusCode.NotFound, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));
    }

    [Fact]
    public async Task TheClaimLimitIsSeparateFromSetupAndLogin()
    {
        using var factory = new DeviceApiFactory(settings =>
        {
            settings["Devices:ClaimAttemptsPerMinute"] = "2";
            settings[AuthRateLimitOptions.AttemptsPerMinuteKey] = "2";
        });
        var token = await factory.SignInAsync();
        using var client = factory.CreateClient();

        Assert.Equal(HttpStatusCode.NotFound, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));
        Assert.Equal(HttpStatusCode.NotFound, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));
        Assert.Equal(HttpStatusCode.TooManyRequests, await StatusAsync(client, DeviceApiFactory.ClaimRequest(DeviceApiFactory.SetupCode, token)));

        // Setup used one login attempt; the claims used none.
        var login = DeviceApiFactory.Json(HttpMethod.Post, "/api/auth/login", """{"username":"Bridger","password":"mosaic-desk-2026"}""");
        Assert.Equal(HttpStatusCode.OK, await StatusAsync(client, login));
    }

    [Fact]
    public async Task NeitherAClaimedNorAMistypedCodeIsLogged()
    {
        var token = await _factory.SignInAsync();
        await using var device = await _factory.ConnectAsync();
        var code = (await device.HandshakeAsync()).PairingPending.Code;
        const string Mistyped = "ABCDEFGH";

        Assert.Equal(HttpStatusCode.NotFound, (await ClaimAsync(Mistyped, token)).Status);
        Assert.Equal(HttpStatusCode.OK, (await ClaimAsync(code, token)).Status);
        await device.ReceiveAsync();

        Assert.NotEmpty(_factory.Logs);
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(code, StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(_factory.Logs, line => line.Contains(Mistyped, StringComparison.OrdinalIgnoreCase));
    }

    private static void AssertProblem(HttpStatusCode status, JsonElement body, HttpStatusCode expected, string code, string title)
    {
        Assert.Equal(expected, status);
        Assert.Equal((int)expected, body.GetProperty("status").GetInt32());
        Assert.Equal(code, body.GetProperty("code").GetString());
        Assert.Equal(title, body.GetProperty("title").GetString());
    }

    private static HttpRequestMessage PlainText(string body, string? token, string? origin = DeviceApiFactory.Origin)
    {
        var request = DeviceApiFactory.Json(HttpMethod.Post, DeviceApi.ClaimPath, body, token, origin);
        request.Content = new StringContent(body, Encoding.UTF8, "text/plain");
        return request;
    }

    private static async Task<HttpStatusCode> StatusAsync(HttpClient client, HttpRequestMessage request)
    {
        using (request)
        {
            using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
            return response.StatusCode;
        }
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

    private Task<(HttpStatusCode Status, JsonElement Body)> ClaimAsync(string code, string? token) =>
        SendAsync(DeviceApiFactory.ClaimRequest(code, token));

    private async Task<(HttpStatusCode Status, JsonElement Body)> SendAsync(HttpRequestMessage request)
    {
        using (request)
        {
            using var client = _factory.CreateClient();
            using var response = await client.SendAsync(request, TestContext.Current.CancellationToken);
            var text = await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken);
            return (response.StatusCode, text.Length == 0 ? default : JsonDocument.Parse(text).RootElement.Clone());
        }
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

    private long DeviceRows()
    {
        using var connection = new MosaicDatabase(Path.Combine(_factory.Directory, "mosaic.db")).OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT COUNT(*) FROM devices";
        return (long)command.ExecuteScalar()!;
    }

    private void Execute(string sql)
    {
        using var connection = new MosaicDatabase(Path.Combine(_factory.Directory, "mosaic.db")).OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }
}
