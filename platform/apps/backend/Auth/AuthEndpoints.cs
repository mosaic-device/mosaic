using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace Mosaic.Server.Auth;

/// <summary>First-run setup, login, and logout (Admin API Contract, sections 2 and 4).</summary>
public static class AuthEndpoints
{
    public static void MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/setup", GetSetup);
        app.MapPost("/api/setup", PostSetupAsync);
        app.MapPost("/api/auth/login", LoginAsync);
        app.MapPost("/api/auth/logout", Logout);
    }

    private static IResult GetSetup(AdminStore admins) =>
        Results.Json(new SetupStatusResponse(!admins.Exists()), AuthJsonContext.Default.SetupStatusResponse);

    // Check order is part of the contract: content type, admin exists, fields, setup code.
    private static async Task<IResult> PostSetupAsync(
        HttpContext context, AdminStore admins, SessionStore sessions, SessionCookie cookie, SetupCode setupCode, PasswordHasher hasher)
    {
        if (!context.Request.HasJsonContentType())
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        if (admins.Exists())
        {
            return Problems.SetupAlreadyCompleted(context);
        }

        var request = await ReadBodyAsync(context, AuthJsonContext.Default.SetupRequest);
        if (request is null)
        {
            return MalformedBody(context);
        }

        var errors = CredentialRules.ValidateSetup(request);
        if (errors.Count > 0)
        {
            return Problems.ValidationFailed(context, errors);
        }

        if (!setupCode.Matches(request.SetupCode!))
        {
            return Problems.InvalidSetupCode(context);
        }

        var admin = admins.TryCreate(request.Username!, hasher.Hash(request.Password!));
        if (admin is null)
        {
            return Problems.SetupAlreadyCompleted(context);
        }

        cookie.Append(context.Response, sessions.Create(admin.Id));
        return Results.Json(
            new AdminResponse(new AdminSummary(admin.Username)),
            AuthJsonContext.Default.AdminResponse,
            statusCode: StatusCodes.Status201Created);
    }

    private static async Task<IResult> LoginAsync(
        HttpContext context, AdminStore admins, SessionStore sessions, SessionCookie cookie, PasswordHasher hasher)
    {
        if (!context.Request.HasJsonContentType())
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        if (!admins.Exists())
        {
            return Problems.SetupRequired(context);
        }

        var request = await ReadBodyAsync(context, AuthJsonContext.Default.LoginRequest);
        if (request is null)
        {
            return MalformedBody(context);
        }

        var errors = CredentialRules.ValidateLogin(request);
        if (errors.Count > 0)
        {
            return Problems.ValidationFailed(context, errors);
        }

        // Unknown usernames and wrong passwords take the same time and get the same response.
        var admin = admins.FindByUsername(request.Username!);
        var verified = admin is null
            ? hasher.VerifyAgainstDummy(request.Password!)
            : hasher.Verify(request.Password!, admin.PasswordHash);
        if (!verified || admin is null)
        {
            return Problems.InvalidCredentials(context);
        }

        cookie.Append(context.Response, sessions.Create(admin.Id));
        return Results.Json(new AdminResponse(new AdminSummary(admin.Username)), AuthJsonContext.Default.AdminResponse);
    }

    private static IResult Logout(HttpContext context, SessionStore sessions, SessionCookie cookie)
    {
        if (SessionCookie.TryRead(context.Request, out var token))
        {
            sessions.Delete(token);
        }

        cookie.Clear(context.Response);
        return Results.NoContent();
    }

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

    private static IResult MalformedBody(HttpContext context) =>
        Problems.ValidationFailed(context, errors: null, "The request body must be a JSON object.");
}
