using System.Globalization;
using Microsoft.Data.Sqlite;
using Mosaic.Server.Data;

namespace Mosaic.Server.Auth;

public sealed record Admin(long Id, string Username, string PasswordHash);

/// <summary>
/// Reads and creates the single admin row. Callers own the connection and any transaction,
/// so several stores can take part in one atomic operation.
/// </summary>
public sealed class AdminStore(TimeProvider time)
{
    private const int SqliteConstraint = 19;

    /// <summary>Whether first-run setup has created the admin yet.</summary>
    public bool Exists(SqliteConnection connection, SqliteTransaction? transaction = null)
    {
        using var command = connection.CreateCommand("SELECT EXISTS (SELECT 1 FROM admin)", transaction);
        return Convert.ToInt64(command.ExecuteScalar(), CultureInfo.InvariantCulture) == 1;
    }

    /// <summary>Usernames match case-insensitively; they are stored as entered.</summary>
    public Admin? FindByUsername(SqliteConnection connection, string username)
    {
        using var command = connection.CreateCommand(
            "SELECT id, username, password_hash FROM admin WHERE lower(username) = lower($username)");
        command.Parameters.AddWithValue("$username", username);
        using var reader = command.ExecuteReader();
        return reader.Read() ? new Admin(reader.GetInt64(0), reader.GetString(1), reader.GetString(2)) : null;
    }

    /// <summary>Creates the one admin, or returns null when one already exists.</summary>
    public Admin? TryCreate(SqliteTransaction transaction, string username, string passwordHash)
    {
        var now = Timestamps.Format(time.GetUtcNow());
        using var command = transaction.Connection!.CreateCommand("""
            INSERT INTO admin (id, username, password_hash, created_at, updated_at)
            VALUES (1, $username, $passwordHash, $now, $now)
            """, transaction);
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
