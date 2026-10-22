using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Data.Sqlite;
using Mosaic.Server.Data;

namespace Mosaic.Server.Auth;

/// <summary>A session token as handed to the browser at <see cref="IssuedAt"/>, on login or renewal.</summary>
public sealed record IssuedSession(string Token, DateTimeOffset IssuedAt, DateTimeOffset IdleExpiresAt, DateTimeOffset AbsoluteExpiresAt)
{
    /// <summary>When the session ends: the earlier of its idle and absolute expiry.</summary>
    public DateTimeOffset ExpiresAt => IdleExpiresAt < AbsoluteExpiresAt ? IdleExpiresAt : AbsoluteExpiresAt;
}

/// <summary>
/// Opaque server-side sessions. The cookie carries a random token; the database stores
/// only its SHA-256 hash, so a leaked database cannot be replayed as a cookie.
/// Callers own the connection and any transaction.
/// </summary>
public sealed class SessionStore(TimeProvider time)
{
    public static readonly TimeSpan IdleLifetime = TimeSpan.FromDays(7);
    public static readonly TimeSpan AbsoluteLifetime = TimeSpan.FromDays(30);

    private const int TokenSize = 32;

    public IssuedSession Create(SqliteConnection connection, long adminId, SqliteTransaction? transaction = null)
    {
        var token = Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(TokenSize));
        var now = time.GetUtcNow();
        var session = new IssuedSession(token, now, now + IdleLifetime, now + AbsoluteLifetime);

        using var command = connection.CreateCommand("""
            INSERT INTO sessions (token_hash, admin_id, created_at, idle_expires_at, absolute_expires_at)
            VALUES ($tokenHash, $adminId, $createdAt, $idleExpiresAt, $absoluteExpiresAt)
            """, transaction);
        command.Parameters.AddWithValue("$tokenHash", HashToken(token));
        command.Parameters.AddWithValue("$adminId", adminId);
        command.Parameters.AddWithValue("$createdAt", Timestamps.Format(now));
        command.Parameters.AddWithValue("$idleExpiresAt", Timestamps.Format(session.IdleExpiresAt));
        command.Parameters.AddWithValue("$absoluteExpiresAt", Timestamps.Format(session.AbsoluteExpiresAt));
        command.ExecuteNonQuery();
        return session;
    }

    public void Delete(SqliteConnection connection, string token)
    {
        using var command = connection.CreateCommand("DELETE FROM sessions WHERE token_hash = $tokenHash");
        command.Parameters.AddWithValue("$tokenHash", HashToken(token));
        command.ExecuteNonQuery();
    }

    public static byte[] HashToken(string token) => SHA256.HashData(Encoding.UTF8.GetBytes(token));
}
