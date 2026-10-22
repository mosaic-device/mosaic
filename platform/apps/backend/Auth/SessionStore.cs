using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text;
using Mosaic.Server.Data;

namespace Mosaic.Server.Auth;

public sealed record IssuedSession(string Token, DateTimeOffset IdleExpiresAt, DateTimeOffset AbsoluteExpiresAt);

/// <summary>
/// Opaque server-side sessions. The cookie carries a random token; the database stores
/// only its SHA-256 hash, so a leaked database cannot be replayed as a cookie.
/// </summary>
public sealed class SessionStore(MosaicDatabase database, TimeProvider time, AuthOptions options)
{
    public const string CookieName = "mosaic_session";
    public static readonly TimeSpan IdleLifetime = TimeSpan.FromDays(7);
    public static readonly TimeSpan AbsoluteLifetime = TimeSpan.FromDays(30);

    private const int TokenSize = 32;

    public IssuedSession Create(long adminId)
    {
        var token = Base64Url.EncodeToString(RandomNumberGenerator.GetBytes(TokenSize));
        var now = time.GetUtcNow();
        var session = new IssuedSession(token, now + IdleLifetime, now + AbsoluteLifetime);

        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO sessions (token_hash, admin_id, created_at, idle_expires_at, absolute_expires_at)
            VALUES ($tokenHash, $adminId, $createdAt, $idleExpiresAt, $absoluteExpiresAt)
            """;
        command.Parameters.AddWithValue("$tokenHash", HashToken(token));
        command.Parameters.AddWithValue("$adminId", adminId);
        command.Parameters.AddWithValue("$createdAt", Timestamps.Format(now));
        command.Parameters.AddWithValue("$idleExpiresAt", Timestamps.Format(session.IdleExpiresAt));
        command.Parameters.AddWithValue("$absoluteExpiresAt", Timestamps.Format(session.AbsoluteExpiresAt));
        command.ExecuteNonQuery();
        return session;
    }

    public void Delete(string token)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM sessions WHERE token_hash = $tokenHash";
        command.Parameters.AddWithValue("$tokenHash", HashToken(token));
        command.ExecuteNonQuery();
    }

    public void AppendCookie(HttpResponse response, IssuedSession session) =>
        response.Cookies.Append(CookieName, session.Token, CookieOptions(IdleLifetime));

    public void ClearCookie(HttpResponse response) =>
        response.Cookies.Append(CookieName, "", CookieOptions(TimeSpan.Zero));

    public static byte[] HashToken(string token) => SHA256.HashData(Encoding.UTF8.GetBytes(token));

    private CookieOptions CookieOptions(TimeSpan maxAge) => new()
    {
        Path = "/",
        HttpOnly = true,
        Secure = options.SecureCookie,
        SameSite = SameSiteMode.Strict,
        MaxAge = maxAge,
    };
}
