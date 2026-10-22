using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace Mosaic.Server.Auth;

/// <summary>
/// HTTP surface for first-run setup, login, and logout (Admin API Contract, sections 2 and 4).
/// Handles content type, body parsing, cookies, and status codes; the rules live in <see cref="AuthService"/>.
/// </summary>
public static class AuthEndpoints
{
    public static void MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/setup", GetSetup);
        app.MapPost("/api/setup", PostSetupAsync);
        app.MapPost("/api/auth/login", LoginAsync);
        app.MapPost("/api/auth/logout", Logout);
    }

    private static IResult GetSetup(AuthService auth) =>
        Results.Json(new SetupStatusResponse(auth.SetupRequired()), AuthJsonContext.Default.SetupStatusResponse);

    private static async Task<IResult> PostSetupAsync(HttpContext context, AuthService auth, SessionCookie cookie)
    {
        if (!context.Request.HasJsonContentType())
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        var request = await ReadBodyAsync(context, AuthJsonContext.Default.SetupRequest);
        return Respond(context, cookie, auth.Setup(request), StatusCodes.Status201Created);
    }

    private static async Task<IResult> LoginAsync(HttpContext context, AuthService auth, SessionCookie cookie)
    {
        if (!context.Request.HasJsonContentType())
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        var request = await ReadBodyAsync(context, AuthJsonContext.Default.LoginRequest);
        return Respond(context, cookie, auth.Login(request), StatusCodes.Status200OK);
    }

    private static IResult Logout(HttpContext context, AuthService auth, SessionCookie cookie)
    {
        auth.Logout(SessionCookie.TryRead(context.Request, out var token) ? token : null);
        cookie.Clear(context.Response);
        return Results.NoContent();
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
            default:
                throw new InvalidOperationException($"Unhandled auth outcome {result.Outcome}.");
        }
    }

    /// <summary>The parsed body, or null when it isn't a JSON object of the expected shape.</summary>
    private static async Task<T?> ReadBodyAsync<T>(HttpContext context, JsonTypeInfo<T> typeInfo)
        where T : class
    {
        try
        {
            return await context.Request.ReadFromJsonAsync(typeInfo, context.RequestAborted);
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
