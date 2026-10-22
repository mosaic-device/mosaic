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
    private readonly AuthService _auth;

    public AuthServiceTests()
    {
        _database = new MosaicDatabase(Path.Combine(_directory, "mosaic.db"));
        _database.Migrate();
        var time = new FixedTimeProvider(new DateTimeOffset(2026, 10, 21, 12, 0, 0, TimeSpan.Zero));
        _auth = new AuthService(
            _database,
            new AdminStore(time),
            new SessionStore(time),
            SetupCode.FromConfiguration(TestConfiguration.From((SetupCode.OverrideKey, Code))),
            new PasswordHasher(PasswordHasher.MinIterations));
    }

    public void Dispose()
    {
        SqliteConnection.ClearAllPools();
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    private static SetupRequest ValidSetup(string code = Code) => new("Bridger", Password, code);

    [Fact]
    public void SetupCreatesTheAdminAndItsFirstSession()
    {
        var result = _auth.Setup(ValidSetup());

        Assert.Equal(AuthOutcome.Succeeded, result.Outcome);
        Assert.Equal("Bridger", result.Admin!.Username);
        Assert.NotNull(result.Session);
        Assert.False(_auth.SetupRequired());
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public void ExistingAdminIsReportedBeforeAnyOtherSetupCheck()
    {
        _auth.Setup(ValidSetup());

        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, _auth.Setup(null).Outcome);
        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, _auth.Setup(new SetupRequest("x", "short", "")).Outcome);
        Assert.Equal(AuthOutcome.SetupAlreadyCompleted, _auth.Setup(ValidSetup("AAAA-BBBB")).Outcome);
    }

    [Fact]
    public void MalformedBodyIsReportedBeforeFieldRules() =>
        Assert.Equal(AuthOutcome.MalformedBody, _auth.Setup(null).Outcome);

    [Fact]
    public void FieldRulesAreCheckedBeforeTheSetupCode()
    {
        var result = _auth.Setup(new SetupRequest("x", Password, "AAAA-BBBB"));

        Assert.Equal(AuthOutcome.ValidationFailed, result.Outcome);
        Assert.Equal(["username"], result.Errors!.Keys);
    }

    [Fact]
    public void WrongSetupCodeCreatesNothing()
    {
        Assert.Equal(AuthOutcome.InvalidSetupCode, _auth.Setup(ValidSetup("AAAA-BBBB")).Outcome);
        Assert.True(_auth.SetupRequired());
    }

    [Fact]
    public void SetupIsAtomicWhenTheSessionCannotBeStored()
    {
        Execute("CREATE TRIGGER fail_sessions BEFORE INSERT ON sessions BEGIN SELECT RAISE(ABORT, 'injected failure'); END");

        Assert.Throws<SqliteException>(() => _auth.Setup(ValidSetup()));

        Assert.True(_auth.SetupRequired());
        Assert.Equal(0L, Count("admin"));

        Execute("DROP TRIGGER fail_sessions");
        Assert.Equal(AuthOutcome.Succeeded, _auth.Setup(ValidSetup()).Outcome);
    }

    [Fact]
    public async Task ConcurrentSetupsCreateExactlyOneAdmin()
    {
        var results = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => _auth.Setup(ValidSetup()))));

        Assert.Single(results, result => result.Outcome == AuthOutcome.Succeeded);
        Assert.All(results.Where(result => result.Outcome != AuthOutcome.Succeeded),
            result => Assert.Equal(AuthOutcome.SetupAlreadyCompleted, result.Outcome));
        Assert.Equal(1L, Count("admin"));
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public void LoginBeforeSetupIsReportedFirst()
    {
        Assert.Equal(AuthOutcome.SetupRequired, _auth.Login(null).Outcome);
        Assert.Equal(AuthOutcome.SetupRequired, _auth.Login(new LoginRequest("Bridger", Password)).Outcome);
    }

    [Fact]
    public void LoginRejectsMalformedAndIncompleteRequests()
    {
        _auth.Setup(ValidSetup());

        Assert.Equal(AuthOutcome.MalformedBody, _auth.Login(null).Outcome);
        Assert.Equal(AuthOutcome.ValidationFailed, _auth.Login(new LoginRequest("Bridger", "")).Outcome);
    }

    [Theory]
    [InlineData("Bridger", "wrong-password-1")]
    [InlineData("nobody", Password)]
    public void LoginFailuresLookTheSame(string username, string password)
    {
        _auth.Setup(ValidSetup());

        var result = _auth.Login(new LoginRequest(username, password));

        Assert.Equal(AuthResult.Failed(AuthOutcome.InvalidCredentials), result);
        Assert.Equal(1L, Count("sessions"));
    }

    [Fact]
    public void LoginMatchesTheUsernameCaseInsensitively()
    {
        _auth.Setup(ValidSetup());

        var result = _auth.Login(new LoginRequest("BRIDGER", Password));

        Assert.Equal(AuthOutcome.Succeeded, result.Outcome);
        Assert.Equal("Bridger", result.Admin!.Username);
        Assert.Equal(2L, Count("sessions"));
    }

    [Fact]
    public void LogoutDeletesOnlyThatSession()
    {
        var first = _auth.Setup(ValidSetup()).Session!;
        var second = _auth.Login(new LoginRequest("Bridger", Password)).Session!;

        _auth.Logout(first.Token);

        Assert.Equal(1L, Count("sessions"));
        Assert.Equal(1L, Scalar($"SELECT COUNT(*) FROM sessions WHERE hex(token_hash) = '{Convert.ToHexString(SessionStore.HashToken(second.Token))}'"));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("not-a-session")]
    public void LogoutWithoutAKnownSessionChangesNothing(string? token)
    {
        _auth.Setup(ValidSetup());

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
