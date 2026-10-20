using Microsoft.Data.Sqlite;

namespace Mosaic.Server.Data;

/// <summary>The server's SQLite database and its versioned schema migrations.</summary>
public sealed class MosaicDatabase
{
    public const string PathKey = "Database:Path";
    public const string DefaultPath = "data/mosaic.db";

    // Append new migrations; never edit or reorder one that has shipped.
    private static readonly Migration[] Migrations =
    [
        new(1, "initial_admin_and_settings", """
            CREATE TABLE admin (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                username TEXT NOT NULL,
                password_hash TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            ) STRICT;

            CREATE TABLE settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL,
                updated_at TEXT NOT NULL
            ) STRICT;
            """),
    ];

    public MosaicDatabase(string path)
    {
        Path = System.IO.Path.GetFullPath(path);
        ConnectionString = new SqliteConnectionStringBuilder
        {
            DataSource = Path,
            Mode = SqliteOpenMode.ReadWriteCreate,
            ForeignKeys = true,
            Pooling = false,
        }.ToString();
    }

    public string Path { get; }

    public string ConnectionString { get; }

    public static int LatestVersion => Migrations[^1].Version;

    /// <summary>Resolves <see cref="PathKey"/> (relative paths against the content root).</summary>
    public static MosaicDatabase FromConfiguration(IConfiguration configuration, string contentRootPath)
    {
        var configured = configuration[PathKey];
        var path = string.IsNullOrWhiteSpace(configured) ? DefaultPath : configured;
        return new MosaicDatabase(System.IO.Path.GetFullPath(path, contentRootPath));
    }

    public SqliteConnection OpenConnection()
    {
        var connection = new SqliteConnection(ConnectionString);
        connection.Open();
        return connection;
    }

    /// <summary>Applies each pending migration once, in its own transaction.</summary>
    /// <returns>The versions applied by this call; empty when the schema is current.</returns>
    public IReadOnlyList<int> Migrate()
    {
        var directory = System.IO.Path.GetDirectoryName(Path);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        using var connection = OpenConnection();
        Execute(connection, null, """
            CREATE TABLE IF NOT EXISTS schema_migrations (
                version INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                applied_at TEXT NOT NULL
            ) STRICT;
            """);

        var applied = new List<int>();
        foreach (var migration in Migrations)
        {
            // BEGIN IMMEDIATE takes the write lock before checking, so concurrent starts cannot both apply it.
            using var transaction = connection.BeginTransaction(deferred: false);
            using (var check = connection.CreateCommand())
            {
                check.Transaction = transaction;
                check.CommandText = "SELECT COUNT(*) FROM schema_migrations WHERE version = $version";
                check.Parameters.AddWithValue("$version", migration.Version);
                if (Convert.ToInt64(check.ExecuteScalar(), System.Globalization.CultureInfo.InvariantCulture) > 0)
                {
                    continue;
                }
            }

            Execute(connection, transaction, migration.Sql);
            using (var record = connection.CreateCommand())
            {
                record.Transaction = transaction;
                record.CommandText = """
                    INSERT INTO schema_migrations (version, name, applied_at)
                    VALUES ($version, $name, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
                    """;
                record.Parameters.AddWithValue("$version", migration.Version);
                record.Parameters.AddWithValue("$name", migration.Name);
                record.ExecuteNonQuery();
            }

            transaction.Commit();
            applied.Add(migration.Version);
        }

        return applied;
    }

    private static void Execute(SqliteConnection connection, SqliteTransaction? transaction, string sql)
    {
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }

    private sealed record Migration(int Version, string Name, string Sql);
}
