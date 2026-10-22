using System.Globalization;
using System.Net;
using System.Text;
using System.Text.Json;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.DependencyInjection;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests;

/// <summary>First-run setup, login, and logout against the Admin API Contract.</summary>
public sealed class AuthEndpointTests : IDisposable
{
    private const string Password = "mosaic-desk-2026";

    private readonly AuthApiFactory _factory = new();
    private readonly HttpClient _client;

    public AuthEndpointTests() => _client = _factory.CreateApiClient();

    public void Dispose()
    {
        _client.Dispose();
        _factory.Dispose();
    }

    [Fact]
    public async Task SetupStatusReportsWhetherAnAdminExists()
    {
        Assert.True(await SetupRequired());

        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        Assert.False(await SetupRequired());
    }

    [Fact]
    public async Task SetupCreatesTheAdminAndLogsThemIn()
    {
        using var response = await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        Assert.Equal("Bridger", (await Body(response)).GetProperty("admin").GetProperty("username").GetString());
        var (token, attributes) = AuthApiFactory.SessionCookie(response);
        Assert.Equal(["httponly", "max-age=604800", "path=/", "samesite=strict", "secure"], attributes);
        Assert.Equal(43, token.Length); // 32 random bytes, Base64Url without padding.

        using var connection = _factory.Database.OpenConnection();
        var stored = Assert.Single(Rows(connection, "SELECT username, password_hash FROM admin"));
        Assert.Equal("Bridger", stored[0]);
        Assert.StartsWith("pbkdf2-sha256$600000$", stored[1], StringComparison.Ordinal);
        Assert.DoesNotContain(Password, stored[1], StringComparison.Ordinal);
        Assert.True(PasswordHasher.Verify(Password, stored[1]));

        var session = Assert.Single(Rows(connection, "SELECT hex(token_hash), created_at, idle_expires_at, absolute_expires_at FROM sessions"));
        Assert.Equal(Convert.ToHexString(SessionStore.HashToken(token)), session[0]);
        Assert.Equal(["2026-10-21T12:00:00.000Z", "2026-10-28T12:00:00.000Z", "2026-11-20T12:00:00.000Z"], session[1..]);
    }

    [Theory]
    [InlineData(AuthApiFactory.Code)]
    [InlineData("wrong-code")]
    public async Task RepeatSetupCannotCreateOrReplaceTheAdmin(string code)
    {
        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        using var response = await SendAsync(AuthApiFactory.Setup("intruder", "another-password-1", code), HttpStatusCode.Conflict);

        await AssertProblem(response, "setup_already_completed");
        using var connection = _factory.Database.OpenConnection();
        Assert.Equal(["Bridger"], Rows(connection, "SELECT username FROM admin").Select(row => row[0]));
    }

    [Fact]
    public async Task SetupWithAnAdminPresentReturnsConflictBeforeValidatingFields()
    {
        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        using var response = await SendAsync(AuthApiFactory.Setup("x", "short", ""), HttpStatusCode.Conflict);

        await AssertProblem(response, "setup_already_completed");
    }

    [Fact]
    public async Task SetupReportsEveryFieldProblemBeforeCheckingTheCode()
    {
        using var response = await SendAsync(AuthApiFactory.Setup("x", "short", ""), HttpStatusCode.BadRequest);

        var errors = (await AssertProblem(response, "validation_failed")).GetProperty("errors");
        Assert.Equal(["password", "setupCode", "username"], errors.EnumerateObject().Select(field => field.Name).Order());
    }

    [Fact]
    public async Task SetupRejectsAWrongCodeOnlyWhenTheFieldsAreValid()
    {
        using var response = await SendAsync(AuthApiFactory.Setup(code: "AAAA-BBBB"), HttpStatusCode.Forbidden);

        await AssertProblem(response, "invalid_setup_code");
        Assert.True(await SetupRequired());
    }

    [Theory]
    [InlineData("k7qf2m9x")]
    [InlineData(" K7QF2M9X ")]
    public async Task SetupCodeIsCaseInsensitiveAndTheDashIsOptional(string code) =>
        await SendAsync(AuthApiFactory.Setup(code: code), HttpStatusCode.Created);

    [Theory]
    [InlineData("ab", Password, "username")]
    [InlineData("has space", Password, "username")]
    [InlineData("ünïcode", Password, "username")]
    [InlineData("abcdefghijklmnopqrstuvwxyz0123456", Password, "username")]
    [InlineData("Bridger", "eleven-char", "password")]
    [InlineData("Bridger", "", "password")]
    public async Task SetupEnforcesFieldRules(string username, string password, string field)
    {
        using var response = await SendAsync(AuthApiFactory.Setup(username, password), HttpStatusCode.BadRequest);

        var errors = (await AssertProblem(response, "validation_failed")).GetProperty("errors");
        Assert.Equal([field], errors.EnumerateObject().Select(error => error.Name));
    }

    [Fact]
    public async Task SetupAcceptsTheFieldLimits()
    {
        var password = new string('p', 128);

        await SendAsync(AuthApiFactory.Setup("A.b_c-12345678901234567890123456", password), HttpStatusCode.Created);

        await SendAsync(AuthApiFactory.Login("a.B_C-12345678901234567890123456", password), HttpStatusCode.OK);
    }

    [Fact]
    public async Task SetupRejectsAPasswordOverTheLimit()
    {
        using var response = await SendAsync(AuthApiFactory.Setup(password: new string('p', 129)), HttpStatusCode.BadRequest);

        await AssertProblem(response, "validation_failed");
    }

    [Fact]
    public async Task LoginBeforeSetupReturnsSetupRequired()
    {
        using var response = await SendAsync(AuthApiFactory.Login(), HttpStatusCode.Conflict);

        await AssertProblem(response, "setup_required");
    }

    [Fact]
    public async Task LoginAcceptsValidCredentialsWithAnyUsernameCase()
    {
        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        using var response = await SendAsync(AuthApiFactory.Login("bRIDGER"), HttpStatusCode.OK);

        Assert.Equal("Bridger", (await Body(response)).GetProperty("admin").GetProperty("username").GetString());
        var (token, attributes) = AuthApiFactory.SessionCookie(response);
        Assert.Contains("max-age=604800", attributes);
        using var connection = _factory.Database.OpenConnection();
        Assert.Equal(2, Rows(connection, "SELECT token_hash FROM sessions").Count);
        Assert.Single(Rows(connection, $"SELECT 1 FROM sessions WHERE hex(token_hash) = '{Convert.ToHexString(SessionStore.HashToken(token))}'"));
    }

    [Fact]
    public async Task InvalidLoginDoesNotRevealWhichFieldFailed()
    {
        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        using var wrongPassword = await SendAsync(AuthApiFactory.Login("Bridger", "not-the-password"), HttpStatusCode.Unauthorized);
        using var unknownUser = await SendAsync(AuthApiFactory.Login("someone-else", Password), HttpStatusCode.Unauthorized);

        var first = await AssertProblem(wrongPassword, "invalid_credentials");
        var second = await AssertProblem(unknownUser, "invalid_credentials");
        Assert.Equal(WithoutTraceId(first), WithoutTraceId(second));
        Assert.False(wrongPassword.Headers.Contains("Set-Cookie"));
        Assert.False(unknownUser.Headers.Contains("Set-Cookie"));
    }

    [Fact]
    public async Task LoginRequiresBothFields()
    {
        await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);

        using var response = await SendAsync(
            AuthApiFactory.Json(HttpMethod.Post, "/api/auth/login", """{"username":""}"""),
            HttpStatusCode.BadRequest);

        var errors = (await AssertProblem(response, "validation_failed")).GetProperty("errors");
        Assert.Equal(["password", "username"], errors.EnumerateObject().Select(field => field.Name).Order());
    }

    [Fact]
    public async Task LogoutDeletesTheSessionAndClearsTheCookie()
    {
        using var setup = await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);
        var (token, _) = AuthApiFactory.SessionCookie(setup);

        using var response = await SendAsync(AuthApiFactory.Logout(token), HttpStatusCode.NoContent);

        var (cleared, attributes) = AuthApiFactory.SessionCookie(response);
        Assert.Equal("", cleared);
        Assert.Contains("max-age=0", attributes);
        Assert.Contains("path=/", attributes);
        using var connection = _factory.Database.OpenConnection();
        Assert.Empty(Rows(connection, "SELECT token_hash FROM sessions"));
    }

    [Fact]
    public async Task LogoutLeavesOtherSessionsAlone()
    {
        using var setup = await SendAsync(AuthApiFactory.Setup(), HttpStatusCode.Created);
        using var login = await SendAsync(AuthApiFactory.Login(), HttpStatusCode.OK);

        await SendAsync(AuthApiFactory.Logout(AuthApiFactory.SessionCookie(setup).Value), HttpStatusCode.NoContent);

        using var connection = _factory.Database.OpenConnection();
        var remaining = Assert.Single(Rows(connection, "SELECT hex(token_hash) FROM sessions"));
        Assert.Equal(Convert.ToHexString(SessionStore.HashToken(AuthApiFactory.SessionCookie(login).Value)), remaining[0]);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("not-a-session")]
    public async Task LogoutAlwaysSucceeds(string? token)
    {
        using var response = await SendAsync(AuthApiFactory.Logout(token), HttpStatusCode.NoContent);

        Assert.Contains("max-age=0", AuthApiFactory.SessionCookie(response).Attributes);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("https://evil.example")]
    [InlineData("http://mosaic.test")]
    [InlineData("null")]
    public async Task StateChangingRequestsRequireAnAllowedOrigin(string? origin)
    {
        foreach (var request in new[]
        {
            AuthApiFactory.Json(HttpMethod.Post, "/api/setup", "{}", origin),
            AuthApiFactory.Json(HttpMethod.Post, "/api/auth/login", "{}", origin),
            AuthApiFactory.Logout(null, origin),
        })
        {
            using var response = await SendAsync(request, HttpStatusCode.Forbidden);
            await AssertProblem(response, "forbidden_origin");
        }
    }

    [Fact]
    public async Task OriginCheckPrecedesTheContentTypeCheck()
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, "/api/setup") { Content = new StringContent("{}", Encoding.UTF8, "text/plain") };
        request.Headers.Add("Origin", "https://evil.example");

        using var response = await SendAsync(request, HttpStatusCode.Forbidden);

        await AssertProblem(response, "forbidden_origin");
    }

    [Fact]
    public async Task ConfiguredAdditionalOriginIsAccepted()
    {
        using var request = AuthApiFactory.Setup();
        request.Headers.Remove("Origin");
        request.Headers.Add("Origin", AuthApiFactory.DevOrigin);

        await SendAsync(request, HttpStatusCode.Created);
    }

    [Theory]
    [InlineData("/api/setup")]
    [InlineData("/api/auth/login")]
    public async Task NonJsonBodiesGetAnEmpty415(string path)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, path)
        {
            Content = new StringContent("username=Bridger", Encoding.UTF8, "application/x-www-form-urlencoded"),
        };
        request.Headers.Add("Origin", AuthApiFactory.PublicOrigin);

        using var response = await SendAsync(request, HttpStatusCode.UnsupportedMediaType);

        Assert.Equal("", await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken));
    }

    [Theory]
    [InlineData("/api/setup", "{\"username\":")]
    [InlineData("/api/setup", "[]")]
    [InlineData("/api/setup", "null")]
    [InlineData("/api/setup", "")]
    [InlineData("/api/setup", "{\"username\":42}")]
    public async Task MalformedJsonIsAValidationFailure(string path, string json)
    {
        using var response = await SendAsync(AuthApiFactory.Json(HttpMethod.Post, path, json), HttpStatusCode.BadRequest);

        await AssertProblem(response, "validation_failed");
    }

    [Fact]
    public async Task TheGeneratedSetupCodeWorksWithoutAnOverride()
    {
        using var factory = new AuthApiFactory(settings => settings.Remove(SetupCode.OverrideKey));
        using var client = factory.CreateApiClient();
        var code = factory.Services.GetRequiredService<SetupCode>();

        Assert.False(code.FromOverride);
        Assert.Matches("^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$", code.Display);
        using var wrong = await client.SendAsync(AuthApiFactory.Setup(code: AuthApiFactory.Code), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.Forbidden, wrong.StatusCode);
        using var right = await client.SendAsync(AuthApiFactory.Setup(code: code.Display), TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.Created, right.StatusCode);
    }

    [Fact]
    public void StartupFailsWithoutAPublicOrigin()
    {
        using var factory = new AuthApiFactory(settings => settings["Auth:PublicOrigin"] = null);

        var error = Assert.ThrowsAny<Exception>(() => factory.CreateApiClient());

        Assert.Contains("Auth:PublicOrigin", Flatten(error), StringComparison.Ordinal);
    }

    private async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, HttpStatusCode expected)
    {
        using (request)
        {
            var response = await _client.SendAsync(request, TestContext.Current.CancellationToken);
            if (response.StatusCode != expected)
            {
                var body = await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken);
                Assert.Fail($"Expected {(int)expected} for {request.Method} {request.RequestUri}, received {(int)response.StatusCode}: {body}");
            }

            return response;
        }
    }

    private async Task<bool> SetupRequired()
    {
        using var response = await _client.GetAsync("/api/setup", TestContext.Current.CancellationToken);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        return (await Body(response)).GetProperty("setupRequired").GetBoolean();
    }

    private static async Task<JsonElement> Body(HttpResponseMessage response) =>
        JsonDocument.Parse(await response.Content.ReadAsStringAsync(TestContext.Current.CancellationToken)).RootElement.Clone();

    private static async Task<JsonElement> AssertProblem(HttpResponseMessage response, string code)
    {
        Assert.Equal("application/problem+json", response.MediaType());
        var body = await Body(response);
        Assert.Equal(code, body.GetProperty("code").GetString());
        Assert.Equal((int)response.StatusCode, body.GetProperty("status").GetInt32());
        Assert.False(string.IsNullOrWhiteSpace(body.GetProperty("title").GetString()));
        Assert.False(string.IsNullOrWhiteSpace(body.GetProperty("traceId").GetString()));
        return body;
    }

    private static string WithoutTraceId(JsonElement problem) =>
        string.Join(",", problem.EnumerateObject().Where(p => p.Name != "traceId").Select(p => $"{p.Name}={p.Value}"));

    private static string Flatten(Exception error) =>
        error.InnerException is null ? error.Message : error.Message + " | " + Flatten(error.InnerException);

    private static List<string[]> Rows(SqliteConnection connection, string sql)
    {
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        using var reader = command.ExecuteReader();
        var rows = new List<string[]>();
        while (reader.Read())
        {
            rows.Add([.. Enumerable.Range(0, reader.FieldCount).Select(i => Convert.ToString(reader.GetValue(i), CultureInfo.InvariantCulture) ?? "")]);
        }

        return rows;
    }
}
