using System.Text.Json;
using System.Text.Json.Serialization.Metadata;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.Extensions.Primitives;

namespace Mosaic.Server.Auth;

/// <summary>
/// HTTP surface for first-run setup, login, and logout (Admin API Contract, sections 2 and 4).
/// Handles content type, body parsing, cookies, and status codes; the rules live in <see cref="AuthService"/>.
/// </summary>
public static class AuthEndpoints
{
    // Setup needs well under 1 KB; anything larger is rejected before it is read.
    public const int MaxBodyBytes = 4096;

    public static void MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/setup", GetSetup);
        app.MapPost("/api/setup", PostSetupAsync).RequireAuthRateLimits();
        app.MapPost("/api/auth/login", LoginAsync).RequireAuthRateLimits();
        app.MapPost("/api/auth/logout", Logout);
    }

    private static IResult GetSetup(AuthService auth) =>
        Results.Json(new SetupStatusResponse(auth.SetupRequired()), AuthJsonContext.Default.SetupStatusResponse);

    private static async Task<IResult> PostSetupAsync(
        HttpContext context, AuthService auth, SessionCookie cookie, AuthRateLimitOptions limits)
    {
        if (!HasSupportedJsonBody(context.Request))
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        // Contract order puts 409 before anything about the body, so answer it without reading one.
        // AuthService repeats the check inside its transaction for setups that race.
        if (!auth.SetupRequired())
        {
            return Problems.SetupAlreadyCompleted(context);
        }

        var body = await ReadBodyAsync(context, AuthJsonContext.Default.SetupRequest, limits.BodyReadTimeout);
        return body.Rejection
            ?? Respond(context, cookie, await auth.SetupAsync(body.Value, context.RequestAborted), StatusCodes.Status201Created);
    }

    private static async Task<IResult> LoginAsync(
        HttpContext context, AuthService auth, SessionCookie cookie, AuthRateLimitOptions limits)
    {
        if (!HasSupportedJsonBody(context.Request))
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        if (auth.SetupRequired())
        {
            return Problems.SetupRequired(context);
        }

        var body = await ReadBodyAsync(context, AuthJsonContext.Default.LoginRequest, limits.BodyReadTimeout);
        return body.Rejection
            ?? Respond(context, cookie, await auth.LoginAsync(body.Value, context.RequestAborted), StatusCodes.Status200OK);
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
            case AuthOutcome.TooManyPasswordChecks:
                // Hashing slots free up within about a second.
                return Problems.TooManyRequests(context, retryAfterSeconds: 1);
            default:
                throw new InvalidOperationException($"Unhandled auth outcome {result.Outcome}.");
        }
    }

    /// <summary>
    /// JSON with no charset or one .NET can decode. An unknown charset is an unsupported media
    /// type, not a server failure, so it gets the same 415 as any other non-JSON body.
    /// </summary>
    private static bool HasSupportedJsonBody(HttpRequest request) =>
        request.HasJsonContentType()
        && request.GetTypedHeaders().ContentType is { } contentType
        && (StringSegment.IsNullOrEmpty(contentType.Charset) || contentType.Encoding is not null);

    /// <summary>
    /// Reads a small JSON body. <see cref="BodyRead{T}.Value"/> is null when the body isn't a JSON
    /// object of the expected shape; <see cref="BodyRead{T}.Rejection"/> is set when the body is
    /// too large (413) or doesn't arrive within <paramref name="timeout"/> (connection closed).
    /// </summary>
    private static async Task<BodyRead<T>> ReadBodyAsync<T>(HttpContext context, JsonTypeInfo<T> typeInfo, TimeSpan timeout)
        where T : class
    {
        var request = context.Request;
        if (request.ContentLength > MaxBodyBytes)
        {
            return new(null, Results.StatusCode(StatusCodes.Status413PayloadTooLarge));
        }

        // Also enforced while reading, for bodies sent without a Content-Length.
        if (context.Features.Get<IHttpMaxRequestBodySizeFeature>() is { IsReadOnly: false } sizeLimit)
        {
            sizeLimit.MaxRequestBodySize = MaxBodyBytes;
        }

        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        deadline.CancelAfter(timeout);
        try
        {
            return new(await request.ReadFromJsonAsync(typeInfo, deadline.Token), null);
        }
        catch (JsonException)
        {
            return new(null, null);
        }
        catch (BadHttpRequestException error) when (error.StatusCode == StatusCodes.Status413PayloadTooLarge)
        {
            return new(null, Results.StatusCode(StatusCodes.Status413PayloadTooLarge));
        }
        catch (OperationCanceledException) when (!context.RequestAborted.IsCancellationRequested)
        {
            // A stalled upload gets no response; closing the connection frees it immediately.
            context.Abort();
            return new(null, Results.Empty);
        }
    }

    private readonly record struct BodyRead<T>(T? Value, IResult? Rejection)
        where T : class;
}
