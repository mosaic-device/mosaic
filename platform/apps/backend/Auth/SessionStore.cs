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
    public DateTimeOffset ExpiresAt => Earlier(IdleExpiresAt, AbsoluteExpiresAt);

    internal static DateTimeOffset Earlier(DateTimeOffset first, DateTimeOffset second) => first < second ? first : second;
}

/// <summary>An unexpired session. <see cref="Renewal"/> is set when this lookup renewed it.</summary>
public sealed record ActiveSession(long AdminId, DateTimeOffset ExpiresAt, IssuedSession? Renewal);

/// <summary>
/// Opaque server-side sessions. The cookie carries a random token; the database stores
/// only its SHA-256 hash, so a leaked database cannot be replayed as a cookie.
/// Callers own the connection and any transaction.
/// </summary>
public sealed class SessionStore(TimeProvider time)
{
    public static readonly TimeSpan IdleLifetime = TimeSpan.FromDays(7);
    public static readonly TimeSpan AbsoluteLifetime = TimeSpan.FromDays(30);

    /// <summary>The shortest gap between two renewals of the same session.</summary>
    public static readonly TimeSpan RenewalInterval = TimeSpan.FromDays(1);

    private const int TokenSize = 32;

    // TokenSize bytes in unpadded Base64Url.
    private const int TokenLength = 43;

    /// <summary>Whether <paramref name="token"/> has the shape of an issued token, so it is worth looking up.</summary>
    public static bool IsWellFormed(string token) =>
        token.Length == TokenLength && Base64Url.IsValid(token, out var decodedLength) && decodedLength == TokenSize;

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

    /// <summary>
    /// Finds the session for <paramref name="token"/> if it hasn't expired. An expired session is
    /// deleted and reported as missing. At most once per <see cref="RenewalInterval"/>, the idle
    /// expiry moves to <see cref="IdleLifetime"/> from now, never past the absolute expiry.
    /// </summary>
    /// <remarks>
    /// Another request can renew or delete the session between this lookup's read and its write,
    /// so each write applies only if the row is still as it was read. When it isn't, one more read
    /// settles the outcome: a session just renewed isn't due again, and a deleted one is gone.
    /// </remarks>
    public ActiveSession? FindActive(SqliteConnection connection, string token) =>
        FindActive(connection, token, HashToken(token), rereadIfChanged: true);

    private ActiveSession? FindActive(SqliteConnection connection, string token, byte[] tokenHash, bool rereadIfChanged)
    {
        long adminId;
        string storedIdleExpiresAt;
        DateTimeOffset idleExpiresAt, absoluteExpiresAt;
        using (var command = connection.CreateCommand(
            "SELECT admin_id, idle_expires_at, absolute_expires_at FROM sessions WHERE token_hash = $tokenHash"))
        {
            command.Parameters.AddWithValue("$tokenHash", tokenHash);
            using var reader = command.ExecuteReader();
            if (!reader.Read())
            {
                return null;
            }

            adminId = reader.GetInt64(0);
            storedIdleExpiresAt = reader.GetString(1);
            idleExpiresAt = Timestamps.Parse(storedIdleExpiresAt);
            absoluteExpiresAt = Timestamps.Parse(reader.GetString(2));
        }

        var now = time.GetUtcNow();
        var expiresAt = IssuedSession.Earlier(idleExpiresAt, absoluteExpiresAt);
        if (now >= expiresAt)
        {
            using var delete = connection.CreateCommand(
                "DELETE FROM sessions WHERE token_hash = $tokenHash AND idle_expires_at = $readIdleExpiresAt");
            delete.Parameters.AddWithValue("$tokenHash", tokenHash);
            delete.Parameters.AddWithValue("$readIdleExpiresAt", storedIdleExpiresAt);
            return delete.ExecuteNonQuery() == 0 && rereadIfChanged
                ? FindActive(connection, token, tokenHash, rereadIfChanged: false)
                : null;
        }

        // Each renewal puts the idle expiry a full IdleLifetime ahead, so more than
        // IdleLifetime - RenewalInterval remaining means the last one was under a day ago.
        // Once the idle expiry has reached the absolute one, renewing cannot extend it.
        if (idleExpiresAt >= absoluteExpiresAt || idleExpiresAt - now > IdleLifetime - RenewalInterval)
        {
            return new ActiveSession(adminId, expiresAt, Renewal: null);
        }

        var renewal = new IssuedSession(token, now, IssuedSession.Earlier(now + IdleLifetime, absoluteExpiresAt), absoluteExpiresAt);
        using var update = connection.CreateCommand(
            "UPDATE sessions SET idle_expires_at = $idleExpiresAt WHERE token_hash = $tokenHash AND idle_expires_at = $readIdleExpiresAt");
        update.Parameters.AddWithValue("$idleExpiresAt", Timestamps.Format(renewal.IdleExpiresAt));
        update.Parameters.AddWithValue("$tokenHash", tokenHash);
        update.Parameters.AddWithValue("$readIdleExpiresAt", storedIdleExpiresAt);
        if (update.ExecuteNonQuery() > 0)
        {
            return new ActiveSession(adminId, renewal.ExpiresAt, renewal);
        }

        // Another request renewed or deleted the session first. If it changes again during the
        // re-read, the lookup fails safe and reports it missing.
        return rereadIfChanged ? FindActive(connection, token, tokenHash, rereadIfChanged: false) : null;
    }

    public void Delete(SqliteConnection connection, string token)
    {
        using var command = connection.CreateCommand("DELETE FROM sessions WHERE token_hash = $tokenHash");
        command.Parameters.AddWithValue("$tokenHash", HashToken(token));
        command.ExecuteNonQuery();
    }

    public static byte[] HashToken(string token) => SHA256.HashData(Encoding.UTF8.GetBytes(token));
}
