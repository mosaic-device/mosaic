namespace Mosaic.Server.Auth;

/// <summary>
/// CSRF Origin check (Admin API Contract, section 4): state-changing API requests must
/// come from an allowed origin. Runs before endpoints so it precedes every other check.
/// </summary>
public static class OriginCheck
{
    public static void UseOriginCheck(this IApplicationBuilder app)
    {
        var options = app.ApplicationServices.GetRequiredService<AuthOptions>();
        app.Use(async (context, next) =>
        {
            if (Rejects(context.Request, options))
            {
                await Problems.ForbiddenOrigin(context).ExecuteAsync(context);
                return;
            }

            await next(context);
        });
    }

    public static bool Rejects(HttpRequest request, AuthOptions options) =>
        IsStateChanging(request.Method)
        && request.Path.StartsWithSegments("/api")
        && !options.IsAllowedOrigin(request.Headers.Origin);

    private static bool IsStateChanging(string method) =>
        HttpMethods.IsPost(method) || HttpMethods.IsPut(method) || HttpMethods.IsPatch(method) || HttpMethods.IsDelete(method);
}
