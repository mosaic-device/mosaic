using System.Globalization;
using Microsoft.Data.Sqlite;
using Mosaic.Server.Auth;
using Mosaic.Server.Data;

namespace Mosaic.Server.Tests.Auth;

/// <summary>Session expiry and renewal against a real SQLite database, with a clock the tests move.</summary>
public sealed class SessionStoreTests : IDisposable
{
    private static readonly DateTimeOffset Start = new(2026, 10, 21, 12, 0, 0, TimeSpan.Zero);

    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));
    private readonly ManualTimeProvider _time = new(Start);
    private readonly SessionStore _sessions;
    private readonly SqliteConnection _connection;
    private readonly IssuedSession _issued;

    public SessionStoreTests()
    {
        var database = new MosaicDatabase(Path.Combine(_directory, "mosaic.db"));
        database.Migrate();
        _sessions = new SessionStore(_time);
        _connection = database.OpenConnection();
        using (var transaction = _connection.BeginTransaction())
        {
            new AdminStore(_time).TryCreate(transaction, "Bridger", "hash");
            transaction.Commit();
        }

        _issued = _sessions.Create(_connection, adminId: 1);
    }

    public void Dispose()
    {
        _connection.Dispose();
        SqliteConnection.ClearAllPools();
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    [Fact]
    public void ANewSessionIsFoundWithItsIdleExpiry()
    {
        var session = _sessions.FindActive(_connection, _issued.Token);

        Assert.Equal(new ActiveSession(1, Start.AddDays(7), Renewal: null), session);
    }

    [Fact]
    public void AnUnknownTokenIsNotFound()
    {
        Assert.Null(_sessions.FindActive(_connection, new string('A', 43)));
        Assert.Equal(1L, Count());
    }

    [Fact]
    public void ASessionUnusedForSevenDaysExpiresAndIsDeleted()
    {
        _time.Advance(TimeSpan.FromDays(7));

        Assert.Null(_sessions.FindActive(_connection, _issued.Token));
        Assert.Equal(0L, Count());
    }

    [Fact]
    public void ASessionUnusedForJustUnderSevenDaysIsStillActive()
    {
        _time.Advance(TimeSpan.FromDays(7) - TimeSpan.FromMilliseconds(1));

        Assert.NotNull(_sessions.FindActive(_connection, _issued.Token));
    }

    [Fact]
    public void ASessionUsedDailyStillEndsThirtyDaysAfterLogin()
    {
        for (var day = 1; day < 30; day++)
        {
            _time.Advance(TimeSpan.FromDays(1));
            Assert.NotNull(_sessions.FindActive(_connection, _issued.Token));
        }

        _time.Advance(TimeSpan.FromDays(1));

        Assert.Null(_sessions.FindActive(_connection, _issued.Token));
        Assert.Equal(0L, Count());
    }

    [Fact]
    public void RenewalHappensAtMostOnceADay()
    {
        _time.Advance(TimeSpan.FromHours(23));
        Assert.Null(_sessions.FindActive(_connection, _issued.Token)!.Renewal);

        _time.Advance(TimeSpan.FromHours(1));
        var renewed = _sessions.FindActive(_connection, _issued.Token)!;
        Assert.Equal(new IssuedSession(_issued.Token, Start.AddDays(1), Start.AddDays(8), Start.AddDays(30)), renewed.Renewal);
        Assert.Equal(Start.AddDays(8), renewed.ExpiresAt);
        Assert.Equal("2026-10-29T12:00:00.000Z", StoredIdleExpiry());

        _time.Advance(TimeSpan.FromHours(23));
        var unchanged = _sessions.FindActive(_connection, _issued.Token)!;
        Assert.Null(unchanged.Renewal);
        Assert.Equal(Start.AddDays(8), unchanged.ExpiresAt);

        _time.Advance(TimeSpan.FromHours(1));
        Assert.Equal(Start.AddDays(9), _sessions.FindActive(_connection, _issued.Token)!.Renewal!.IdleExpiresAt);
    }

    [Fact]
    public void RenewalNeverExtendsPastTheAbsoluteExpiry()
    {
        foreach (var day in new[] { 6, 12, 18 })
        {
            _time.Advance(TimeSpan.FromDays(6));
            Assert.Equal(Start.AddDays(day + 7), _sessions.FindActive(_connection, _issued.Token)!.ExpiresAt);
        }

        _time.Advance(TimeSpan.FromDays(6));
        var capped = _sessions.FindActive(_connection, _issued.Token)!;
        Assert.Equal(new IssuedSession(_issued.Token, Start.AddDays(24), Start.AddDays(30), Start.AddDays(30)), capped.Renewal);
        Assert.Equal(Start.AddDays(30), capped.ExpiresAt);

        // The idle expiry has reached the absolute one, so there is nothing left to renew.
        _time.Advance(TimeSpan.FromDays(5));
        Assert.Equal(new ActiveSession(1, Start.AddDays(30), Renewal: null), _sessions.FindActive(_connection, _issued.Token));

        _time.Advance(TimeSpan.FromDays(1));
        Assert.Null(_sessions.FindActive(_connection, _issued.Token));
    }

    [Fact]
    public void IssuedTokensAreWellFormed() => Assert.True(SessionStore.IsWellFormed(_issued.Token));

    [Theory]
    [InlineData("")]
    [InlineData("abc")]
    [InlineData("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")] // 42 characters.
    [InlineData("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")] // 44 characters.
    [InlineData("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA+")] // Standard Base64, not Base64Url.
    [InlineData("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAé")]
    public void OtherTokensAreMalformed(string token) => Assert.False(SessionStore.IsWellFormed(token));

    private long Count()
    {
        using var command = _connection.CreateCommand();
        command.CommandText = "SELECT COUNT(*) FROM sessions";
        return Convert.ToInt64(command.ExecuteScalar(), CultureInfo.InvariantCulture);
    }

    private string StoredIdleExpiry()
    {
        using var command = _connection.CreateCommand();
        command.CommandText = "SELECT idle_expires_at FROM sessions";
        return (string)command.ExecuteScalar()!;
    }
}
