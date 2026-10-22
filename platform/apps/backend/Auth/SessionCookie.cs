namespace Mosaic.Server.Auth;

/// <summary>Reads and writes the <c>mosaic_session</c> cookie (Admin API Contract, section 4).</summary>
public sealed class SessionCookie(AuthOptions options)
{
    public const string Name = "mosaic_session";

    public static bool TryRead(HttpRequest request, out string token)
    {
        token = request.Cookies.TryGetValue(Name, out var value) ? value ?? "" : "";
        return token.Length > 0;
    }

    public void Append(HttpResponse response, IssuedSession session) =>
        response.Cookies.Append(Name, session.Token, Options(SessionStore.IdleLifetime));

    public void Clear(HttpResponse response) =>
        response.Cookies.Append(Name, "", Options(TimeSpan.Zero));

    private CookieOptions Options(TimeSpan maxAge) => new()
    {
        Path = "/",
        HttpOnly = true,
        // From configuration, not the request scheme: Tailscale Funnel forwards plain HTTP.
        Secure = options.SecureCookie,
        SameSite = SameSiteMode.Strict,
        MaxAge = maxAge,
    };
}
