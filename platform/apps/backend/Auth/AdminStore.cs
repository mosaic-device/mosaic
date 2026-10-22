using System.Globalization;
using Microsoft.Data.Sqlite;
using Mosaic.Server.Data;

namespace Mosaic.Server.Auth;

public sealed record Admin(long Id, string Username, string PasswordHash);

/// <summary>Reads and creates the single admin row.</summary>
public sealed class AdminStore(MosaicDatabase database, TimeProvider time)
{
    private const int SqliteConstraint = 19;

    /// <summary>Whether first-run setup has created the admin yet.</summary>
    public bool Exists()
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT EXISTS (SELECT 1 FROM admin)";
        return Convert.ToInt64(command.ExecuteScalar(), CultureInfo.InvariantCulture) == 1;
    }

    /// <summary>Usernames match case-insensitively; they are stored as entered.</summary>
    public Admin? FindByUsername(string username)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "SELECT id, username, password_hash FROM admin WHERE lower(username) = lower($username)";
        command.Parameters.AddWithValue("$username", username);
        using var reader = command.ExecuteReader();
        return reader.Read() ? new Admin(reader.GetInt64(0), reader.GetString(1), reader.GetString(2)) : null;
    }

    /// <summary>Creates the one admin, or returns null when one already exists.</summary>
    public Admin? TryCreate(string username, string passwordHash)
    {
        var now = Timestamps.Format(time.GetUtcNow());
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO admin (id, username, password_hash, created_at, updated_at)
            VALUES (1, $username, $passwordHash, $now, $now)
            """;
        command.Parameters.AddWithValue("$username", username);
        command.Parameters.AddWithValue("$passwordHash", passwordHash);
        command.Parameters.AddWithValue("$now", now);
        try
        {
            command.ExecuteNonQuery();
        }
        catch (SqliteException error) when (error.SqliteErrorCode == SqliteConstraint)
        {
            // The admin table holds at most one row (CHECK (id = 1)), so a concurrent setup lost the race.
            return null;
        }

        return new Admin(1, username, passwordHash);
    }
}
