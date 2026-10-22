using Microsoft.AspNetCore.Http.Features;

namespace Mosaic.Server.Auth;

/// <summary>
/// Protects admin endpoints (Admin API Contract, sections 2 and 4). Every request without a valid
/// session gets the same <c>401 unauthenticated</c>, so the response never reveals whether a
/// token was missing, malformed, unknown, expired, or logged out. When a session cookie was
/// sent, the 401 also clears it, whatever the reason.
/// </summary>
public static class AdminSessionRequirement
{
    /// <summary>
    /// Requires a signed-in admin, renewing the session cookie when the lookup renewed the session.
    /// Handlers read the admin with <see cref="GetAdminSession"/>.
    /// </summary>
    public static TBuilder RequireAdminSession<TBuilder>(this TBuilder builder)
        where TBuilder : IEndpointConventionBuilder =>
        builder.AddEndpointFilter(async (invocation, next) =>
        {
            var context = invocation.HttpContext;
            var services = context.RequestServices;
            var cookie = services.GetRequiredService<SessionCookie>();
            var sent = SessionCookie.TryRead(context.Request, out var token);
            var session = services.GetRequiredService<AuthService>().Authenticate(sent ? token : null);
            if (session is null)
            {
                // A cookie that no longer works is cleared so the browser stops sending it.
                if (sent)
                {
                    cookie.Clear(context.Response);
                }

                return Problems.Unauthenticated(context);
            }

            if (session.Session.Renewal is { } renewal)
            {
                cookie.Append(context.Response, renewal);
            }

            context.Features.Set(session);
            return await next(invocation);
        });

    /// <summary>The admin authenticated by <see cref="RequireAdminSession"/> for this request.</summary>
    public static AdminSession GetAdminSession(this HttpContext context) => context.Features.GetRequiredFeature<AdminSession>();
}
