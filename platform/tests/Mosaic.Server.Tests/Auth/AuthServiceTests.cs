using System.Globalization;
using Microsoft.Data.Sqlite;
using Mosaic.Server.Auth;
using Mosaic.Server.Data;

namespace Mosaic.Server.Tests.Auth;

/// <summary>Auth rules against a real SQLite database, without HTTP.</summary>
public sealed class AuthServiceTests : IDisposable
{
    private const string Code = "K7QF-2M9X";
    private const string Password = "mosaic-desk-2026";

    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));
    private readonly MosaicDatabase _database;
    private readonly PasswordCheckLimiter _passwordChecks;
    private readonly AuthService _auth;

    public AuthServiceTests()
    {
        _database = new MosaicDatabase(Path.Combine(_directory, "mosaic.db"));
        _database.Migrate();
        var time = new FixedTimeProvider(new DateTimeOffset(2026, 10, 21, 12, 0, 0, TimeSpan.Zero));
        // One slot and no queue, so a test can hold the slot and observe rejection deterministically.
        _passwordChecks = new PasswordCheckLimiter(AuthRateLimitOptions.FromConfiguration(TestConfiguration.From(
            (AuthRateLimitOptions.ConcurrentPasswordChecksKey, "1"),
            (AuthRateLimitOptions.QueuedPasswordChecksKey, "0"))));
        _auth = new AuthService(
            _database,
            new AdminStore(time),
            new SessionStore(time),
            SetupCode.FromConfiguration(TestConfiguration.From((SetupCode.OverrideKey, Code))),
            new PasswordHasher(PasswordHasher.MinIterations),
            _passwordChecks);
    }

    public void Dispose()
    {
        _passwordChecks.Dispose();
        SqliteConnection.ClearAllPools();
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    private static SetupRequest ValidSetup(string code = Code) => new("Bridger", Password, code);

    private Task<AuthResult> Setup(SetupRequest? request) => _auth.SetupAsync(request, TestContext.Current.CancellationToken);

    private Task<AuthResult> Login(LoginRequest? request) => _auth.LoginAsync(request, TestContext.Current.CancellationToken);

    [Fact]
    public async Task SetupCreatesTheAdminAndItsFirstSession()
    {
        var result = await Setup(ValidSetup());

        Assert.Equal(AuthOutcome.Succeeded, result.Outcome);
        Assert.Equal("Bridger", result.Admin!.Username);
        Assert.NotNull(result.Session);
        Assert.False(_auth.SetupRequired());
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public async Task ExistingAdminIsReportedBeforeAnyOtherSetupCheck()
    {
        await Setup(ValidSetup());

        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, (await Setup(null)).Outcome);
        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, (await Setup(new SetupRequest("x", "short", ""))).Outcome);
        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, (await Setup(ValidSetup("AAAA-BBBB"))).Outcome);
    }

    [Fact]
    public async Task MalformedBodyIsReportedBeforeFieldRules() =>
        Assert.Equal(AuthOutcome.MalformedBody, (await Setup(null)).Outcome);

    [Fact]
    public async Task FieldRulesAreCheckedBeforeTheSetupCode()
    {
        var result = await Setup(new SetupRequest("x", Password, "AAAA-BBBB"));

        Assert.Equal(AuthOutcome.ValidationFailed, result.Outcome);
        Assert.Equal(["username"], result.Errors!.Keys);
    }

    [Fact]
    public async Task WrongSetupCodeCreatesNothing()
    {
        Assert.Equal(AuthOutcome.InvalidSetupCode, (await Setup(ValidSetup("AAAA-BBBB"))).Outcome);
        Assert.True(_auth.SetupRequired());
    }

    [Fact]
    public async Task SetupIsAtomicWhenTheSessionCannotBeStored()
    {
        Execute("CREATE TRIGGER fail_sessions BEFORE INSERT ON sessions BEGIN SELECT RAISE(ABORT, 'injected failure'); END");

        await Assert.ThrowsAsync<SqliteException>(() => Setup(ValidSetup()));

        Assert.True(_auth.SetupRequired());
        Assert.Equal(0L, Count("admin"));

        Execute("DROP TRIGGER fail_sessions");
        Assert.Equal(AuthOutcome.Succeeded, (await Setup(ValidSetup())).Outcome);
    }

    [Fact]
    public async Task ConcurrentSetupsCreateExactlyOneAdmin()
    {
        using var limiter = new PasswordCheckLimiter(AuthRateLimitOptions.FromConfiguration(TestConfiguration.From(
            (AuthRateLimitOptions.ConcurrentPasswordChecksKey, "8"))));
        var time = TimeProvider.System;
        var auth = new AuthService(
            _database,
            new AdminStore(time),
            new SessionStore(time),
            SetupCode.FromConfiguration(TestConfiguration.From((SetupCode.OverrideKey, Code))),
            new PasswordHasher(PasswordHasher.MinIterations),
            limiter);

        var results = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => auth.SetupAsync(ValidSetup()))));

        Assert.Single(results, result => result.Outcome == AuthOutcome.Succeeded);
        Assert.All(results.Where(result => result.Outcome != AuthOutcome.Succeeded),
            result => Assert.Equal(AuthOutcome.SetupAlreadyCompleted, result.Outcome));
        Assert.Equal(1L, Count("admin"));
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public async Task SetupIsRejectedWhileEveryHashingSlotIsBusy()
    {
        using (var held = await _passwordChecks.AcquireAsync(TestContext.Current.CancellationToken))
        {
            Assert.True(held.IsAcquired);

            Assert.Equal(AuthOutcome.TooManyPasswordChecks, (await Setup(ValidSetup())).Outcome);
            Assert.True(_auth.SetupRequired());
        }

        Assert.Equal(AuthOutcome.Succeeded, (await Setup(ValidSetup())).Outcome);
    }

    [Fact]
    public async Task LoginIsRejectedWhileEveryHashingSlotIsBusy()
    {
        await Setup(ValidSetup());

        using (var held = await _passwordChecks.AcquireAsync(TestContext.Current.CancellationToken))
        {
            Assert.True(held.IsAcquired);

            Assert.Equal(AuthOutcome.TooManyPasswordChecks, (await Login(new LoginRequest("Bridger", Password))).Outcome);
            Assert.Equal(AuthOutcome.TooManyPasswordChecks, (await Login(new LoginRequest("nobody", Password))).Outcome);
        }

        Assert.Equal(AuthOutcome.Succeeded, (await Login(new LoginRequest("Bridger", Password))).Outcome);
    }

    [Fact]
    public async Task ChecksThatNeedNoHashingIgnoreTheHashingLimit()
    {
        using var held = await _passwordChecks.AcquireAsync(TestContext.Current.CancellationToken);

        Assert.Equal(AuthOutcome.SetupRequired, (await Login(new LoginRequest("Bridger", Password))).Outcome);
        Assert.Equal(AuthOutcome.MalformedBody, (await Setup(null)).Outcome);
        Assert.Equal(AuthOutcome.ValidationFailed, (await Setup(new SetupRequest("x", "short", ""))).Outcome);
        Assert.Equal(AuthOutcome.InvalidSetupCode, (await Setup(ValidSetup("AAAA-BBBB"))).Outcome);
    }

    [Fact]
    public async Task LoginBeforeSetupIsReportedFirst()
    {
        Assert.Equal(AuthOutcome.SetupRequired, (await Login(null)).Outcome);
        Assert.Equal(AuthOutcome.SetupRequired, (await Login(new LoginRequest("Bridger", Password))).Outcome);
    }

    [Fact]
    public async Task LoginRejectsMalformedAndIncompleteRequests()
    {
        await Setup(ValidSetup());

        Assert.Equal(AuthOutcome.MalformedBody, (await Login(null)).Outcome);
        Assert.Equal(AuthOutcome.ValidationFailed, (await Login(new LoginRequest("Bridger", ""))).Outcome);
    }

    [Theory]
    [InlineData("Bridger", "wrong-password-1")]
    [InlineData("nobody", Password)]
    public async Task LoginFailuresLookTheSame(string username, string password)
    {
        await Setup(ValidSetup());

        var result = await Login(new LoginRequest(username, password));

        Assert.Equal(AuthResult.Failed(AuthOutcome.InvalidCredentials), result);
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public async Task LoginMatchesTheUsernameCaseInsensitively()
    {
        await Setup(ValidSetup());

        var result = await Login(new LoginRequest("BRIDGER", Password));

        Assert.Equal(AuthOutcome.Succeeded, result.Outcome);
        Assert.Equal("Bridger", result.Admin!.Username);
        Assert.Equal(2L, Count("sessions"));
    }

    [Fact]
    public async Task LogoutDeletesOnlyThatSession()
    {
        var first = (await Setup(ValidSetup())).Session!;
        var second = (await Login(new LoginRequest("Bridger", Password))).Session!;

        _auth.Logout(first.Token);

        Assert.Equal(1L, Count("sessions"));
        Assert.Equal(1L, Scalar($"SELECT COUNT(*) FROM sessions WHERE hex(token_hash) = '{Convert.ToHexString(SessionStore.HashToken(second.Token))}'"));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("not-a-session")]
    public async Task LogoutWithoutAKnownSessionChangesNothing(string? token)
    {
        await Setup(ValidSetup());

        _auth.Logout(token);

        Assert.Equal(1L, Count("sessions"));
    }

    private long Count(string table) => Scalar($"SELECT COUNT(*) FROM {table}");

    private long Scalar(string sql)
    {
        using var connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        return Convert.ToInt64(command.ExecuteScalar(), CultureInfo.InvariantCulture);
    }

    private void Execute(string sql)
    {
        using var connection = _database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }
}
