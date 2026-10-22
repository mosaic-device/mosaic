using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace Mosaic.Server.Auth;

/// <summary>First-run setup, login, and logout (Admin API Contract, sections 2 and 4).</summary>
public static class AuthEndpoints
{
    private const int MinUsernameLength = 3;
    private const int MaxUsernameLength = 32;
    private const int MinPasswordLength = 12;
    private const int MaxPasswordLength = 128;

    public static void MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/setup", GetSetup);
        app.MapPost("/api/setup", PostSetupAsync);
        app.MapPost("/api/auth/login", LoginAsync);
        app.MapPost("/api/auth/logout", Logout);
    }

    /// <summary>
    /// CSRF Origin check: state-changing API requests must come from an allowed origin.
    /// Runs before endpoints so it precedes every other check, including content type.
    /// </summary>
    public static void UseOriginCheck(this IApplicationBuilder app)
    {
        var options = app.ApplicationServices.GetRequiredService<AuthOptions>();
        app.Use(async (context, next) =>
        {
            var request = context.Request;
            var isStateChanging = HttpMethods.IsPost(request.Method) || HttpMethods.IsPut(request.Method)
                || HttpMethods.IsPatch(request.Method) || HttpMethods.IsDelete(request.Method);
            if (isStateChanging && request.Path.StartsWithSegments("/api") && !options.IsAllowedOrigin(request.Headers.Origin))
            {
                await Problems.ForbiddenOrigin(context).ExecuteAsync(context);
                return;
            }

            await next(context);
        });
    }

    private static IResult GetSetup(AdminStore admins) =>
        Results.Json(new SetupStatusResponse(!admins.Exists()), AuthJsonContext.Default.SetupStatusResponse);

    // Check order is part of the contract: content type, admin exists, fields, setup code.
    private static async Task<IResult> PostSetupAsync(HttpContext context, AdminStore admins, SessionStore sessions, SetupCode setupCode)
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

        var errors = new Dictionary<string, string[]>();
        ValidateUsername(request.Username, errors);
        ValidatePassword(request.Password, errors);
        if (string.IsNullOrWhiteSpace(request.SetupCode))
        {
            errors["setupCode"] = ["Required."];
        }

        if (errors.Count > 0)
        {
            return Problems.ValidationFailed(context, errors);
        }

        if (!setupCode.Matches(request.SetupCode!))
        {
            return Problems.InvalidSetupCode(context);
        }

        var admin = admins.TryCreate(request.Username!, PasswordHasher.Hash(request.Password!));
        if (admin is null)
        {
            return Problems.SetupAlreadyCompleted(context);
        }

        sessions.AppendCookie(context.Response, sessions.Create(admin.Id));
        return Results.Json(
            new AdminResponse(new AdminSummary(admin.Username)),
            AuthJsonContext.Default.AdminResponse,
            statusCode: StatusCodes.Status201Created);
    }

    private static async Task<IResult> LoginAsync(HttpContext context, AdminStore admins, SessionStore sessions)
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

        var errors = new Dictionary<string, string[]>();
        if (string.IsNullOrEmpty(request.Username))
        {
            errors["username"] = ["Required."];
        }

        if (string.IsNullOrEmpty(request.Password))
        {
            errors["password"] = ["Required."];
        }

        if (errors.Count > 0)
        {
            return Problems.ValidationFailed(context, errors);
        }

        // Unknown usernames and wrong passwords take the same time and get the same response.
        var admin = admins.FindByUsername(request.Username!);
        var verified = admin is null
            ? PasswordHasher.VerifyAgainstDummy(request.Password!)
            : PasswordHasher.Verify(request.Password!, admin.PasswordHash);
        if (!verified || admin is null)
        {
            return Problems.InvalidCredentials(context);
        }

        sessions.AppendCookie(context.Response, sessions.Create(admin.Id));
        return Results.Json(new AdminResponse(new AdminSummary(admin.Username)), AuthJsonContext.Default.AdminResponse);
    }

    private static IResult Logout(HttpContext context, SessionStore sessions)
    {
        if (context.Request.Cookies.TryGetValue(SessionStore.CookieName, out var token) && !string.IsNullOrEmpty(token))
        {
            sessions.Delete(token);
        }

        sessions.ClearCookie(context.Response);
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

    private static void ValidateUsername(string? username, Dictionary<string, string[]> errors)
    {
        if (string.IsNullOrEmpty(username))
        {
            errors["username"] = ["Required."];
        }
        else if (username.Length is < MinUsernameLength or > MaxUsernameLength)
        {
            errors["username"] = [$"Must be {MinUsernameLength} to {MaxUsernameLength} characters."];
        }
        else if (!username.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-'))
        {
            errors["username"] = ["Use only letters, digits, '.', '_' or '-'."];
        }
    }

    private static void ValidatePassword(string? password, Dictionary<string, string[]> errors)
    {
        if (string.IsNullOrEmpty(password))
        {
            errors["password"] = ["Required."];
        }
        else if (password.Length < MinPasswordLength)
        {
            errors["password"] = [$"Must be at least {MinPasswordLength} characters."];
        }
        else if (password.Length > MaxPasswordLength)
        {
            errors["password"] = [$"Must be at most {MaxPasswordLength} characters."];
        }
    }
}
