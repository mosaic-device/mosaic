using System.Net;
using System.Net.Http.Headers;
using System.Text;

namespace Mosaic.Server.Tests;

/// <summary>
/// A JSON request body that sends its first byte, then waits until <see cref="Finish"/> is
/// called (or never), simulating a slow or stalled upload.
/// </summary>
public sealed class StalledJsonContent : HttpContent
{
    private readonly byte[] _json;
    private readonly TaskCompletionSource _firstChunkSent = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource _finish = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public StalledJsonContent(string json)
    {
        _json = Encoding.UTF8.GetBytes(json);
        Headers.ContentType = new MediaTypeHeaderValue("application/json") { CharSet = "utf-8" };
    }

    public Task FirstChunkSent => _firstChunkSent.Task;

    public void Finish() => _finish.TrySetResult();

    protected override async Task SerializeToStreamAsync(Stream stream, TransportContext? context)
    {
        await stream.WriteAsync(_json.AsMemory(0, 1));
        await stream.FlushAsync();
        _firstChunkSent.TrySetResult();
        await _finish.Task;
        await stream.WriteAsync(_json.AsMemory(1));
    }

    protected override bool TryComputeLength(out long length)
    {
        length = _json.Length;
        return true;
    }
}
