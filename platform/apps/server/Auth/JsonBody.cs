using System.Text.Json;
using System.Text.Json.Serialization.Metadata;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.Extensions.Primitives;

namespace Mosaic.Server.Auth;

/// <summary>
/// Small JSON request bodies for the admin API: the content type check (<c>415</c>), the
/// 4 KB limit (<c>413</c>) and the read deadline (Admin API Contract, section 3).
/// </summary>
public static class JsonBody
{
    /// <summary>The largest request body any admin endpoint reads.</summary>
    public const int MaxBytes = 4096;

    /// <summary>
    /// JSON with no charset or one .NET can decode. An unknown charset is an unsupported media
    /// type, not a server failure, so it gets the same 415 as any other non-JSON body.
    /// </summary>
    public static bool IsSupported(HttpRequest request) =>
        request.HasJsonContentType()
        && request.GetTypedHeaders().ContentType is { } contentType
        && (StringSegment.IsNullOrEmpty(contentType.Charset) || contentType.Encoding is not null);

    /// <summary>
    /// Reads a small JSON body. <see cref="BodyRead{T}.Value"/> is null when the body isn't a JSON
    /// object of the expected shape; <see cref="BodyRead{T}.Rejection"/> is set when the body is
    /// too large (413) or doesn't arrive within <paramref name="timeout"/> (connection closed).
    /// </summary>
    public static async Task<BodyRead<T>> ReadAsync<T>(HttpContext context, JsonTypeInfo<T> typeInfo, TimeSpan timeout)
        where T : class
    {
        var request = context.Request;
        if (request.ContentLength > MaxBytes)
        {
            return new(null, Results.StatusCode(StatusCodes.Status413PayloadTooLarge));
        }

        // Also enforced while reading, for bodies sent without a Content-Length.
        if (context.Features.Get<IHttpMaxRequestBodySizeFeature>() is { IsReadOnly: false } sizeLimit)
        {
            sizeLimit.MaxRequestBodySize = MaxBytes;
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
}

/// <summary>The outcome of <see cref="JsonBody.ReadAsync"/>.</summary>
public readonly record struct BodyRead<T>(T? Value, IResult? Rejection)
    where T : class;
