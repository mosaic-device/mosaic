using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Configuration;
using Mosaic.Server.Data;

namespace Mosaic.Server.Tests;

public sealed class MosaicDatabaseTests : IDisposable
{
    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));

    private string DatabasePath => Path.Combine(_directory, "nested", "mosaic.db");

    public void Dispose()
    {
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    [Fact]
    public void EmptyDatabaseReceivesInitialSchema()
    {
        var database = new MosaicDatabase(DatabasePath);

        var applied = database.Migrate();

        Assert.Equal([1, 2], applied);
        Assert.True(File.Exists(DatabasePath));
        using var connection = database.OpenConnection();
        Assert.Equal(
            ["admin", "schema_migrations", "sessions", "settings"],
            Strings(connection, "SELECT name FROM sqlite_schema WHERE type = 'table' ORDER BY name"));
        Assert.Equal(
            ["id", "username", "password_hash", "created_at", "updated_at"],
            Strings(connection, "SELECT name FROM pragma_table_info('admin')"));
        Assert.Equal(
            ["key", "value", "updated_at"],
            Strings(connection, "SELECT name FROM pragma_table_info('settings')"));
        Assert.Equal(
            ["token_hash", "admin_id", "created_at", "idle_expires_at", "absolute_expires_at"],
            Strings(connection, "SELECT name FROM pragma_table_info('sessions')"));
        Assert.Equal(
            ["1:initial_admin_and_settings", "2:admin_sessions"],
            Strings(connection, "SELECT version || ':' || name FROM schema_migrations ORDER BY version"));
    }

    [Fact]
    public void DeletingTheAdminDeletesItsSessions()
    {
        var database = new MosaicDatabase(DatabasePath);
        database.Migrate();
        using var connection = database.OpenConnection();
        Execute(connection, """
            INSERT INTO admin VALUES (1, 'admin', 'hash', 'now', 'now');
            INSERT INTO sessions VALUES (x'01', 1, 'now', 'later', 'later');
            DELETE FROM admin;
            """);

        Assert.Equal(0L, Scalar(connection, "SELECT COUNT(*) FROM sessions"));
    }

    [Fact]
    public void ReapplyingMigrationsIsANoOp()
    {
        var database = new MosaicDatabase(DatabasePath);
        database.Migrate();

        Assert.Empty(database.Migrate());
        Assert.Empty(new MosaicDatabase(DatabasePath).Migrate());

        using var connection = database.OpenConnection();
        Assert.Equal(2L, Scalar(connection, "SELECT COUNT(*) FROM schema_migrations"));
    }

    [Fact]
    public void SavedDataSurvivesReopeningAndMigratingAgain()
    {
        var first = new MosaicDatabase(DatabasePath);
        first.Migrate();
        using (var connection = first.OpenConnection())
        {
            Execute(connection, """
                INSERT INTO admin (id, username, password_hash, created_at, updated_at)
                VALUES (1, 'admin', 'stored-hash', '2026-10-18T00:00:00Z', '2026-10-18T00:00:00Z');
                INSERT INTO settings (key, value, updated_at) VALUES ('display.name', 'Kitchen', '2026-10-18T00:00:00Z');
                """);
        }

        var reopened = new MosaicDatabase(DatabasePath);
        Assert.Empty(reopened.Migrate());

        using var check = reopened.OpenConnection();
        Assert.Equal(["admin|stored-hash"], Strings(check, "SELECT username || '|' || password_hash FROM admin"));
        Assert.Equal(["Kitchen"], Strings(check, "SELECT value FROM settings WHERE key = 'display.name'"));
    }

    [Fact]
    public void AdminTableHoldsASingleAdmin()
    {
        var database = new MosaicDatabase(DatabasePath);
        database.Migrate();
        using var connection = database.OpenConnection();
        Execute(connection, "INSERT INTO admin VALUES (1, 'admin', 'hash', 'now', 'now')");

        var error = Assert.Throws<SqliteException>(() =>
            Execute(connection, "INSERT INTO admin VALUES (2, 'second', 'hash', 'now', 'now')"));

        Assert.Equal(19, error.SqliteErrorCode); // SQLITE_CONSTRAINT
        Assert.Equal(1L, Scalar(connection, "SELECT COUNT(*) FROM admin"));
    }

    [Fact]
    public void PathComesFromConfigurationRelativeToContentRoot()
    {
        var empty = new ConfigurationBuilder().Build();
        var configured = new ConfigurationBuilder()
            .AddInMemoryCollection(new Dictionary<string, string?> { [MosaicDatabase.PathKey] = "state/custom.db" })
            .Build();

        Assert.Equal(
            Path.Combine(_directory, "data", "mosaic.db"),
            MosaicDatabase.FromConfiguration(empty, _directory).Path);
        Assert.Equal(
            Path.Combine(_directory, "state", "custom.db"),
            MosaicDatabase.FromConfiguration(configured, _directory).Path);
    }

    private static void Execute(SqliteConnection connection, string sql)
    {
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }

    private static object? Scalar(SqliteConnection connection, string sql)
    {
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        return command.ExecuteScalar();
    }

    private static List<string> Strings(SqliteConnection connection, string sql)
    {
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        using var reader = command.ExecuteReader();
        var values = new List<string>();
        while (reader.Read())
        {
            values.Add(reader.GetString(0));
        }

        return values;
    }
}
