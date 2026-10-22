using System.Diagnostics;

namespace Mosaic.Server.Auth;

/// <summary>The contract's error responses (Admin API Contract, section 3).</summary>
public static class Problems
{
    public const string ContentType = "application/problem+json";

    public static IResult ValidationFailed(HttpContext context, IReadOnlyDictionary<string, string[]>? errors, string title = "Some fields need attention.") =>
        Create(context, StatusCodes.Status400BadRequest, "validation_failed", title, errors);

    public static IResult InvalidCredentials(HttpContext context) =>
        Create(context, StatusCodes.Status401Unauthorized, "invalid_credentials", "Incorrect username or password.");

    public static IResult InvalidSetupCode(HttpContext context) =>
        Create(context, StatusCodes.Status403Forbidden, "invalid_setup_code", "The setup code is incorrect.");

    public static IResult ForbiddenOrigin(HttpContext context) =>
        Create(context, StatusCodes.Status403Forbidden, "forbidden_origin", "This request did not come from the admin app.");

    public static IResult SetupAlreadyCompleted(HttpContext context) =>
        Create(context, StatusCodes.Status409Conflict, "setup_already_completed", "Setup has already been completed.");

    public static IResult SetupRequired(HttpContext context) =>
        Create(context, StatusCodes.Status409Conflict, "setup_required", "Create the admin account before logging in.");

    private static IResult Create(HttpContext context, int status, string code, string title, IReadOnlyDictionary<string, string[]>? errors = null) =>
        Results.Json(
            new ApiProblem(status, code, title, Activity.Current?.Id ?? context.TraceIdentifier, errors),
            AuthJsonContext.Default.ApiProblem,
            ContentType,
            status);
}
