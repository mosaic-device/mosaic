using System.Diagnostics;
using System.Net.WebSockets;
using System.Threading.Channels;
using Google.Protobuf;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// A device that stops reading leaves the server's write waiting. Against a socket double whose
/// writes wait until they are cancelled or the socket is aborted, as a real one's do
/// (<see cref="StalledSocketTests"/>).
/// </summary>
public sealed class DeviceWriteDeadlineTests : IDisposable
{
    private static readonly TimeSpan WriteTimeout = TimeSpan.FromSeconds(1);

    // Write timeout plus close grace, with room for a slow test machine.
    private static readonly TimeSpan Bound = TimeSpan.FromSeconds(5);

    private readonly DeviceApiFactory _factory = new(settings => settings["Devices:WriteTimeout"] = "00:00:01");

    public enum Stall
    {
        /// <summary>A notification can't be written.</summary>
        Send,

        /// <summary>The close frame can't be written.</summary>
        Close,
    }

    public enum During
    {
        Nothing,
        Removal,
        Replacement,
        Shutdown,
    }

    public void Dispose() => _factory.Dispose();

    public static TheoryData<Stall, During> Cases()
    {
        var cases = new TheoryData<Stall, During>();
        foreach (var stall in Enum.GetValues<Stall>())
        {
            foreach (var during in Enum.GetValues<During>())
            {
                cases.Add(stall, during);
            }
        }

        return cases;
    }

    [Theory]
    [MemberData(nameof(Cases))]
    public async Task AStuckWriteEndsTheConnectionWithinItsBound(Stall stall, During during)
    {
        // Resolved up front: shutdown disposes the host's services.
        var handler = _factory.Services.GetRequiredService<DeviceConnectionHandler>();
        var connections = _factory.Connections;
        var devices = _factory.Devices;
        var lifetime = _factory.Services.GetRequiredService<IHostApplicationLifetime>();
        var identity = TestDevice.Create();
        var id = _factory.Pair(identity);
        // The paired frame always goes out; the stall is the notification after it, or the close.
        var socket = new StallingSocket(identity.Connect().ToByteArray(), stallSendsFrom: stall == Stall.Send ? 1 : int.MaxValue, stallClose: stall == Stall.Close);
        var run = Task.Run(() => handler.RunAsync(socket), TestContext.Current.CancellationToken);
        await WaitUntil(() => socket.Sent.Count == 1);

        Task<NotificationOutcome>? outcome = null;
        if (stall == Stall.Send)
        {
            outcome = await StartNotificationAsync(id, identity);
        }
        else
        {
            // A text frame is closed with 1003.
            socket.Push(WebSocketMessageType.Text, [0x61]);
        }

        await socket.Stalled.WaitAsync(Bound, TestContext.Current.CancellationToken);
        var elapsed = Stopwatch.StartNew();
        FakeDevice? replacement = null;
        switch (during)
        {
            case During.Removal:
                devices.Revoke(id);
                connections.NotifyRevoked(identity.SecretHash);
                break;
            case During.Replacement:
                replacement = await _factory.ConnectAsync(identity);
                Assert.Equal(PayloadCase.Paired, (await replacement.HandshakeAsync()).PayloadCase);
                break;
            case During.Shutdown:
                lifetime.StopApplication();
                break;
        }

        await run.WaitAsync(Bound, TestContext.Current.CancellationToken);

        Assert.InRange(elapsed.Elapsed, TimeSpan.Zero, Bound);
        Assert.Equal(WebSocketState.Aborted, socket.State);
        Assert.False(socket.Overlapped);
        Assert.Equal(0, socket.WritesAfterAbort);
        Assert.Contains(_factory.Logs, line => line.Contains("stopped reading", StringComparison.Ordinal));
        if (stall == Stall.Send)
        {
            Assert.Equal(PayloadCase.Notification, BackendToDeviceEnvelope.Parser.ParseFrom(socket.Sent[1]).PayloadCase);
            Assert.Equal(NotificationOutcome.NoAnswer, await outcome!.WaitAsync(Bound, TestContext.Current.CancellationToken));
        }
        else
        {
            Assert.Equal(WebSocketCloseStatus.InvalidMessageType, socket.CloseStatus);
        }

        if (replacement is not null)
        {
            // The newer connection took over and is unaffected.
            await using (replacement)
            {
                Assert.True(connections.IsConnected(identity.SecretHash));
                Assert.Equal(WebSocketState.Open, replacement.Socket.State);
            }
        }
        else
        {
            Assert.False(connections.IsConnected(identity.SecretHash));
        }

        if (during == During.Removal)
        {
            // The removal stands even though the device was never told.
            Assert.Equal(CredentialState.Revoked, devices.Find(identity.SecretHash).State);
            Assert.DoesNotContain(socket.Sent, frame => BackendToDeviceEnvelope.Parser.ParseFrom(frame).PayloadCase == PayloadCase.Rejected);
        }
    }

    [Fact]
    public async Task AWriteThatFinishesInTimeKeepsTheConnection()
    {
        var handler = _factory.Services.GetRequiredService<DeviceConnectionHandler>();
        var identity = TestDevice.Create();
        var id = _factory.Pair(identity);
        // Writes wait, but less than the timeout.
        var socket = new StallingSocket(identity.Connect().ToByteArray(), stallSendsFrom: int.MaxValue, stallClose: false, delay: WriteTimeout / 2);
        var run = Task.Run(() => handler.RunAsync(socket), TestContext.Current.CancellationToken);
        await WaitUntil(() => socket.Sent.Count == 1);

        await StartNotificationAsync(id, identity);
        await WaitUntil(() => socket.Sent.Count == 2);
        await Task.Delay(WriteTimeout, TestContext.Current.CancellationToken);

        Assert.False(run.IsCompleted);
        Assert.Equal(WebSocketState.Open, socket.State);
        Assert.True(_factory.Connections.IsConnected(identity.SecretHash));
        Assert.DoesNotContain(_factory.Logs, line => line.Contains("stopped reading", StringComparison.Ordinal));

        // Removal still rejects and closes as usual.
        _factory.Devices.Revoke(id);
        _factory.Connections.NotifyRevoked(identity.SecretHash);
        await run.WaitAsync(Bound, TestContext.Current.CancellationToken);
        Assert.Equal(PayloadCase.Rejected, BackendToDeviceEnvelope.Parser.ParseFrom(socket.Sent[^1]).PayloadCase);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, socket.CloseStatus);
        Assert.Equal(WebSocketState.CloseSent, socket.State);
    }

    public enum AtShutdown
    {
        /// <summary>A notification write is already stuck.</summary>
        StuckWrite,

        /// <summary>A slow notification write finishes, then the close frame is stuck.</summary>
        SlowWriteThenStuckClose,
    }

    [Theory]
    [InlineData(AtShutdown.StuckWrite)]
    [InlineData(AtShutdown.SlowWriteThenStuckClose)]
    public async Task ShutdownEndsDeviceWritesWithinTheHostsLimit(AtShutdown at)
    {
        // A write timeout well past the host's shutdown limit, which the settings allow: each
        // write alone would hold shutdown past the limit.
        var shutdownTimeout = TimeSpan.FromSeconds(3);
        using var factory = new DeviceApiFactory(
            settings => settings["Devices:WriteTimeout"] = "00:00:20",
            services: services => services.Configure<HostOptions>(host => host.ShutdownTimeout = shutdownTimeout));
        var handler = factory.Services.GetRequiredService<DeviceConnectionHandler>();
        var connections = factory.Connections;
        var lifetime = factory.Services.GetRequiredService<IHostApplicationLifetime>();
        var identity = TestDevice.Create();
        var id = factory.Pair(identity);
        var socket = at == AtShutdown.StuckWrite
            ? new StallingSocket(identity.Connect().ToByteArray(), stallSendsFrom: 1, stallClose: false)
            : new StallingSocket(identity.Connect().ToByteArray(), stallSendsFrom: int.MaxValue, stallClose: true, delay: TimeSpan.FromSeconds(1));
        var run = Task.Run(() => handler.RunAsync(socket), TestContext.Current.CancellationToken);
        await WaitUntil(() => socket.Sent.Count == 1);

        var outcome = await StartNotificationAsync(factory, id, identity);
        if (at == AtShutdown.StuckWrite)
        {
            await socket.Stalled.WaitAsync(Bound, TestContext.Current.CancellationToken);
        }
        else
        {
            // The notification is being written.
            await WaitUntil(() => socket.Sent.Count == 2);
        }

        var elapsed = Stopwatch.StartNew();
        lifetime.StopApplication();
        await run.WaitAsync(shutdownTimeout + Bound, TestContext.Current.CancellationToken);

        // Ended by the shutdown deadline, a second short of the host's limit.
        Assert.InRange(elapsed.Elapsed, shutdownTimeout - TimeSpan.FromSeconds(1.5), shutdownTimeout);
        Assert.Equal(WebSocketState.Aborted, socket.State);
        Assert.False(socket.Overlapped);
        Assert.Equal(0, socket.WritesAfterAbort);
        Assert.False(connections.IsConnected(identity.SecretHash));
        Assert.Contains(factory.Logs, line => line.Contains("at shutdown", StringComparison.Ordinal));
        Assert.Equal(PayloadCase.Notification, BackendToDeviceEnvelope.Parser.ParseFrom(socket.Sent[1]).PayloadCase);
        if (at == AtShutdown.SlowWriteThenStuckClose)
        {
            // The slow write finished and the server went on to close for shutdown.
            Assert.Equal(WebSocketCloseStatus.EndpointUnavailable, socket.CloseStatus);
        }
        else
        {
            Assert.Equal(NotificationOutcome.NoAnswer, await outcome.WaitAsync(Bound, TestContext.Current.CancellationToken));
        }
    }

    /// <summary>Starts a test notification once the connection has been told it is paired.</summary>
    private Task<Task<NotificationOutcome>> StartNotificationAsync(long id, TestDevice identity) =>
        StartNotificationAsync(_factory, id, identity);

    private static async Task<Task<NotificationOutcome>> StartNotificationAsync(DeviceApiFactory factory, long id, TestDevice identity)
    {
        var delivery = factory.Services.GetRequiredService<NotificationDelivery>();
        DeliveryStart? start = null;
        await WaitUntil(() =>
        {
            start = delivery.Start(id, identity.SecretHash, DeviceApi.TestNotification(factory.Time.GetUtcNow()));
            return start.Kind == DeliveryStartKind.Started;
        });
        return start!.Outcome!;
    }

    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (!condition())
        {
            Assert.True(DateTime.UtcNow < deadline, "Timed out waiting for the condition.");
            await Task.Delay(25, TestContext.Current.CancellationToken);
        }
    }

    /// <summary>
    /// A device that sends <c>connect</c>, then whatever is pushed. Writes from
    /// <paramref name="stallSendsFrom"/> on, and the close if <paramref name="stallClose"/>, wait
    /// until cancelled or the socket is aborted; an abort lets the write finish without an error,
    /// as Kestrel's can. Other writes take <paramref name="delay"/>.
    /// </summary>
    private sealed class StallingSocket(byte[] connect, int stallSendsFrom, bool stallClose, TimeSpan delay = default) : WebSocket
    {
        private readonly Lock _lock = new();
        private readonly List<byte[]> _sent = [];
        private readonly Channel<(WebSocketMessageType Type, byte[] Bytes)> _incoming = Channel.CreateUnbounded<(WebSocketMessageType, byte[])>();
        private readonly TaskCompletionSource _aborted = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource _stalled = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private WebSocketState _state = WebSocketState.Open;
        private WebSocketCloseStatus? _closeStatus;
        private int _writing;
        private int _writesAfterAbort;
        private bool _overlapped;

        public IReadOnlyList<byte[]> Sent
        {
            get
            {
                lock (_lock)
                {
                    return [.. _sent];
                }
            }
        }

        /// <summary>Completes when a write starts waiting.</summary>
        public Task Stalled => _stalled.Task;

        public bool Overlapped => Volatile.Read(ref _overlapped);

        public int WritesAfterAbort => Volatile.Read(ref _writesAfterAbort);

        public override WebSocketCloseStatus? CloseStatus => _closeStatus;

        public override string? CloseStatusDescription => null;

        public override WebSocketState State => _state;

        public override string? SubProtocol => null;

        public void Push(WebSocketMessageType type, byte[] bytes) => _incoming.Writer.TryWrite((type, bytes));

        public override void Abort()
        {
            _state = WebSocketState.Aborted;
            _aborted.TrySetResult();
        }

        public override Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken) =>
            throw new NotSupportedException();

        public override async Task CloseOutputAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken)
        {
            await WriteAsync(() => _closeStatus = closeStatus, stallClose, cancellationToken);
            if (_state == WebSocketState.Open)
            {
                _state = WebSocketState.CloseSent;
                Push(WebSocketMessageType.Close, []);
            }
        }

        public override async Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken cancellationToken)
        {
            var (type, bytes) = Interlocked.Exchange(ref connect, null!) is { } first
                ? (WebSocketMessageType.Binary, first)
                : await NextIncomingAsync();
            bytes.CopyTo(buffer.AsSpan());
            return new WebSocketReceiveResult(bytes.Length, type, endOfMessage: true);
        }

        public override Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType, bool endOfMessage, CancellationToken cancellationToken)
        {
            var frame = buffer.ToArray();
            int index;
            lock (_lock)
            {
                index = _sent.Count;
            }

            return WriteAsync(
                () =>
                {
                    lock (_lock)
                    {
                        _sent.Add(frame);
                    }
                },
                index >= stallSendsFrom,
                cancellationToken);
        }

        public override void Dispose()
        {
        }

        private async Task<(WebSocketMessageType, byte[])> NextIncomingAsync()
        {
            var next = _incoming.Reader.ReadAsync().AsTask();
            await Task.WhenAny(next, _aborted.Task);
            return next.IsCompleted
                ? await next
                : throw new WebSocketException(WebSocketError.ConnectionClosedPrematurely);
        }

        private async Task WriteAsync(Action record, bool stall, CancellationToken cancellationToken)
        {
            if (_state == WebSocketState.Aborted)
            {
                Interlocked.Increment(ref _writesAfterAbort);
                throw new WebSocketException(WebSocketError.InvalidState);
            }

            if (Interlocked.Increment(ref _writing) > 1)
            {
                Volatile.Write(ref _overlapped, true);
            }

            try
            {
                record();
                if (stall)
                {
                    _stalled.TrySetResult();
                    await _aborted.Task.WaitAsync(cancellationToken);
                }
                else if (delay > TimeSpan.Zero)
                {
                    await Task.Delay(delay, cancellationToken);
                }
            }
            finally
            {
                Interlocked.Decrement(ref _writing);
            }
        }
    }
}
