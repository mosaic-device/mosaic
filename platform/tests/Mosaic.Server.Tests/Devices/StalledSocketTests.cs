using System.Diagnostics;
using System.Net;
using System.Net.WebSockets;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// What a real socket through Kestrel does when its peer stops reading and a server send is left
/// waiting, with no device protocol on top. A device can't make the server write enough to fill
/// the buffers, so the server's own handling of a stuck write is tested against a socket double
/// (<see cref="DeviceWriteDeadlineTests"/>); these establish the transport behavior it relies on.
/// </summary>
public sealed class StalledSocketTests
{
    [Fact]
    public async Task KeepAliveAbortsASendWaitingOnAPeerThatStoppedReading()
    {
        await using var server = await StalledServer.StartAsync(TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1));
        var (socket, pending) = await server.Stalled;
        var elapsed = Stopwatch.StartNew();

        // The waiting send ends once a ping goes unanswered. It may end without an error, so the
        // socket's state is what shows the connection is gone.
        await Record.ExceptionAsync(() => pending.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken));

        Assert.True(pending.IsCompleted);
        Assert.InRange(elapsed.Elapsed, TimeSpan.Zero, TimeSpan.FromSeconds(5));
        Assert.Equal(WebSocketState.Aborted, socket.State);
        await Assert.ThrowsAsync<WebSocketException>(() =>
            socket.SendAsync(new ArraySegment<byte>([1]), WebSocketMessageType.Binary, true, CancellationToken.None));
    }

    [Fact]
    public async Task CancellingAWaitingSendAbortsTheSocket()
    {
        // Keep-alive off, so only the cancellation can end the send.
        await using var server = await StalledServer.StartAsync(TimeSpan.Zero, TimeSpan.Zero);
        var (socket, pending) = await server.Stalled;

        // Still waiting a while later: nothing else would end it.
        await Task.Delay(TimeSpan.FromMilliseconds(500), TestContext.Current.CancellationToken);
        Assert.False(pending.IsCompleted);

        var elapsed = Stopwatch.StartNew();
        await server.CancelSendAsync();
        await Record.ExceptionAsync(() => pending.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken));

        Assert.True(pending.IsCompleted);
        Assert.InRange(elapsed.Elapsed, TimeSpan.Zero, TimeSpan.FromSeconds(2));
        Assert.Equal(WebSocketState.Aborted, socket.State);
    }

    /// <summary>
    /// A Kestrel server whose WebSocket endpoint sends to a client that never reads, until a send
    /// is left waiting. It keeps that connection open until disposed.
    /// </summary>
    private sealed class StalledServer : IAsyncDisposable
    {
        private readonly TaskCompletionSource<(WebSocket Socket, Task Pending)> _stalled = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource _release = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly CancellationTokenSource _sends = new();
        private readonly ClientWebSocket _client = new();
        private WebApplication? _app;

        public Task<(WebSocket Socket, Task Pending)> Stalled =>
            _stalled.Task.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);

        public static async Task<StalledServer> StartAsync(TimeSpan keepAliveInterval, TimeSpan keepAliveTimeout)
        {
            var server = new StalledServer();
            var builder = WebApplication.CreateSlimBuilder();
            builder.WebHost.UseKestrel(options => options.Listen(IPAddress.Loopback, 0));
            server._app = builder.Build();
            server._app.UseWebSockets();
            server._app.Map("/ws", async (HttpContext context) =>
            {
                using var socket = await context.WebSockets.AcceptWebSocketAsync(new WebSocketAcceptContext
                {
                    KeepAliveInterval = keepAliveInterval,
                    KeepAliveTimeout = keepAliveTimeout == TimeSpan.Zero ? Timeout.InfiniteTimeSpan : keepAliveTimeout,
                });
                await server.SendUntilStalledAsync(socket);
            });
            await server._app.StartAsync(TestContext.Current.CancellationToken);

            // The client never reads, so its buffers fill and it never answers a ping.
            server._client.Options.KeepAliveInterval = TimeSpan.Zero;
            var address = new Uri(server._app.Urls.First());
            await server._client.ConnectAsync(new Uri($"ws://{address.Authority}/ws"), TestContext.Current.CancellationToken);
            return server;
        }

        public Task CancelSendAsync() => _sends.CancelAsync();

        public async ValueTask DisposeAsync()
        {
            _release.TrySetResult();
            _client.Abort();
            _client.Dispose();
            if (_app is not null)
            {
                await _app.StopAsync(CancellationToken.None);
                await _app.DisposeAsync();
            }

            _sends.Dispose();
        }

        private async Task SendUntilStalledAsync(WebSocket socket)
        {
            var payload = new ArraySegment<byte>(new byte[4096]);
            while (true)
            {
                var send = socket.SendAsync(payload, WebSocketMessageType.Binary, endOfMessage: true, _sends.Token);

                // Kestrel holds a send while it hands the bytes to the socket; one still waiting
                // after this has nowhere to go.
                await Task.WhenAny(send, Task.Delay(TimeSpan.FromMilliseconds(200)));
                if (!send.IsCompleted)
                {
                    _stalled.TrySetResult((socket, send));
                    await Record.ExceptionAsync(() => send);
                    await _release.Task;
                    return;
                }

                await send;
            }
        }
    }
}
