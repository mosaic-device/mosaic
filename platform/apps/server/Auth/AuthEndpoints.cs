namespace Mosaic.Server.Auth;

/// <summary>
/// HTTP surface for first-run setup, login, logout, and the current session (Admin API Contract, sections 2 and 4).
/// Handles content type, body parsing, cookies, and status codes; the rules live in <see cref="AuthService"/>.
/// </summary>
public static class AuthEndpoints
{
    // Setup needs well under 1 KB; anything larger is rejected before it is read.
    public const int MaxBodyBytes = JsonBody.MaxBytes;

    public static void MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/setup", GetSetup);
        app.MapPost("/api/setup", PostSetupAsync).RequireAuthRateLimits();
        app.MapPost("/api/auth/login", LoginAsync).RequireAuthRateLimits();
        app.MapPost("/api/auth/logout", Logout);
        app.MapGet("/api/auth/session", GetSession).RequireAdminSession();
    }

    private static IResult GetSetup(AuthService auth) =>
        Results.Json(new SetupStatusResponse(auth.SetupRequired()), AuthJsonContext.Default.SetupStatusResponse);

    private static async Task<IResult> PostSetupAsync(
        HttpContext context, AuthService auth, SessionCookie cookie, AuthRateLimitOptions limits)
    {
        if (!JsonBody.IsSupported(context.Request))
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        // Contract order puts 409 before anything about the body, so answer it without reading one.
        // AuthService repeats the check inside its transaction for setups that race.
        if (!auth.SetupRequired())
        {
            return Problems.SetupAlreadyCompleted(context);
        }

        var body = await JsonBody.ReadAsync(context, AuthJsonContext.Default.SetupRequest, limits.BodyReadTimeout);
        return body.Rejection
            ?? Respond(context, cookie, await auth.SetupAsync(body.Value, context.RequestAborted), StatusCodes.Status201Created);
    }

    private static async Task<IResult> LoginAsync(
        HttpContext context, AuthService auth, SessionCookie cookie, AuthRateLimitOptions limits)
    {
        if (!JsonBody.IsSupported(context.Request))
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        if (auth.SetupRequired())
        {
            return Problems.SetupRequired(context);
        }

        var body = await JsonBody.ReadAsync(context, AuthJsonContext.Default.LoginRequest, limits.BodyReadTimeout);
        return body.Rejection
            ?? Respond(context, cookie, await auth.LoginAsync(body.Value, context.RequestAborted), StatusCodes.Status200OK);
    }

    private static IResult Logout(HttpContext context, AuthService auth, SessionCookie cookie)
    {
        auth.Logout(SessionCookie.TryRead(context.Request, out var token) ? token : null);
        cookie.Clear(context.Response);
        return Results.NoContent();
    }

    private static IResult GetSession(HttpContext context)
    {
        var session = context.GetAdminSession();
        // Whole seconds, as the contract shows, rounded down like the cookie's Max-Age.
        return Results.Json(
            new SessionResponse(new AdminSummary(session.Admin.Username), Timestamps.WholeSeconds(session.Session.ExpiresAt)),
            AuthJsonContext.Default.SessionResponse);
    }

    private static IResult Respond(HttpContext context, SessionCookie cookie, AuthResult result, int successStatus)
    {
        switch (result.Outcome)
        {
            case AuthOutcome.Succeeded:
                cookie.Append(context.Response, result.Session!);
                return Results.Json(
                    new AdminResponse(new AdminSummary(result.Admin!.Username)),
                    AuthJsonContext.Default.AdminResponse,
                    statusCode: successStatus);
            case AuthOutcome.MalformedBody:
                return Problems.ValidationFailed(context, errors: null, "The request body must be a JSON object.");
            case AuthOutcome.ValidationFailed:
                return Problems.ValidationFailed(context, result.Errors);
            case AuthOutcome.SetupAlreadyCompleted:
                return Problems.SetupAlreadyCompleted(context);
            case AuthOutcome.InvalidSetupCode:
                return Problems.InvalidSetupCode(context);
            case AuthOutcome.SetupRequired:
                return Problems.SetupRequired(context);
            case AuthOutcome.InvalidCredentials:
                return Problems.InvalidCredentials(context);
            case AuthOutcome.TooManyPasswordChecks:
                // Hashing slots free up within about a second.
                return Problems.TooManyRequests(context, retryAfterSeconds: 1);
            default:
                throw new InvalidOperationException($"Unhandled auth outcome {result.Outcome}.");
        }
    }
}
