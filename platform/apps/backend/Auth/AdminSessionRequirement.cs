using Microsoft.AspNetCore.Http.Features;

namespace Mosaic.Server.Auth;

/// <summary>
/// Protects admin endpoints (Admin API Contract, sections 2 and 4). Every request without a valid
/// session gets the same <c>401 unauthenticated</c>, so the response never reveals whether a
/// token was missing, malformed, unknown, expired, or logged out.
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
            var session = services.GetRequiredService<AuthService>()
                .Authenticate(SessionCookie.TryRead(context.Request, out var token) ? token : null);
            if (session is null)
            {
                return Problems.Unauthenticated(context);
            }

            if (session.Session.Renewal is { } renewal)
            {
                services.GetRequiredService<SessionCookie>().Append(context.Response, renewal);
            }

            context.Features.Set(session);
            return await next(invocation);
        });

    /// <summary>The admin authenticated by <see cref="RequireAdminSession"/> for this request.</summary>
    public static AdminSession GetAdminSession(this HttpContext context) => context.Features.GetRequiredFeature<AdminSession>();
}
