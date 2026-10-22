using Microsoft.AspNetCore.Http.Features;

namespace Mosaic.Server.Auth;

/// <summary>
/// Protects admin endpoints (Admin API Contract, sections 2 and 4). Every request without a valid
/// session gets the same <c>401 unauthenticated</c>, so the response never reveals whether a
/// token was missing, malformed, unknown, expired, or logged out. When a session cookie was
/// sent, the 401 also clears it, whatever the reason. Responses carry <c>Cache-Control: no-store</c>.
/// </summary>
/// <remarks>
/// Endpoints opt in with <see cref="RequireAdminSession"/>, and <see cref="UseAdminSessions"/>
/// enforces it as middleware, before the endpoint binds any parameters. An anonymous request
/// therefore gets the 401 whatever the handler takes, and its body is never read.
/// </remarks>
public static class AdminSessionRequirement
{
    /// <summary>Requires a signed-in admin. Handlers read the admin with <see cref="GetAdminSession"/>.</summary>
    public static TBuilder RequireAdminSession<TBuilder>(this TBuilder builder)
        where TBuilder : IEndpointConventionBuilder
    {
        builder.WithMetadata(Required.Instance);
        // Fails closed: without UseAdminSessions in the pipeline the endpoint throws instead of running unprotected.
        builder.AddEndpointFilter((invocation, next) =>
            invocation.HttpContext.Features.Get<AdminSession>() is null
                ? throw new InvalidOperationException($"{nameof(UseAdminSessions)} must run before endpoints that require an admin session.")
                : next(invocation));
        return builder;
    }

    /// <summary>Authenticates requests to endpoints marked with <see cref="RequireAdminSession"/>.</summary>
    public static void UseAdminSessions(this IApplicationBuilder app)
    {
        var auth = app.ApplicationServices.GetRequiredService<AuthService>();
        var cookie = app.ApplicationServices.GetRequiredService<SessionCookie>();
        app.Use(async (context, next) =>
        {
            if (context.GetEndpoint()?.Metadata.GetMetadata<Required>() is null)
            {
                await next(context);
                return;
            }

            // Admin data and session answers (200 or 401) are per-session and go stale on logout or expiry.
            context.Response.Headers.CacheControl = "no-store";

            var sent = SessionCookie.TryRead(context.Request, out var token);
            var session = auth.Authenticate(sent ? token : null);
            if (session is null)
            {
                // A cookie that no longer works is cleared so the browser stops sending it.
                if (sent)
                {
                    cookie.Clear(context.Response);
                }

                await Problems.Unauthenticated(context).ExecuteAsync(context);
                return;
            }

            if (session.Session.Renewal is { } renewal)
            {
                cookie.Append(context.Response, renewal);
            }

            context.Features.Set(session);
            await next(context);
        });
    }

    /// <summary>The admin authenticated by <see cref="UseAdminSessions"/> for this request.</summary>
    public static AdminSession GetAdminSession(this HttpContext context) => context.Features.GetRequiredFeature<AdminSession>();

    /// <summary>Endpoint metadata marking an endpoint as requiring an admin session.</summary>
    private sealed class Required
    {
        public static readonly Required Instance = new();
    }
}
