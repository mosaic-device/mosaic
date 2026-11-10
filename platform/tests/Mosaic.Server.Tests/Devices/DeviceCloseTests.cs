using System.Diagnostics;
using System.Net.WebSockets;
using Google.Protobuf;
using Microsoft.Extensions.DependencyInjection;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;
using PayloadCase = Mosaic.Device.V1.BackendToDeviceEnvelope.PayloadOneofCase;

namespace Mosaic.Server.Tests.Devices;

/// <summary>How the server closes a device connection, against a socket double that a real client can't imitate.</summary>
public sealed class DeviceCloseTests : IDisposable
{
    // An envelope whose only field is an unknown payload, field 15.
    private static readonly byte[] UnknownPayload = Convert.FromHexString("7A020801");

    private readonly DeviceApiFactory _factory = new();

    public void Dispose() => _factory.Dispose();

    [Fact]
    public async Task ADeviceThatIgnoresTheCloseIsDroppedAfterTheGracePeriod()
    {
        var handler = _factory.Services.GetRequiredService<DeviceConnectionHandler>();
        var identity = TestDevice.Create();
        // After its connect, the device always has another frame ready and never closes.
        var socket = new FloodingSocket(identity.Connect().ToByteArray(), UnknownPayload);
        var elapsed = Stopwatch.StartNew();

        // A pending device that sends anything is rejected, which closes the connection.
        var run = Task.Run(() => handler.RunAsync(socket), TestContext.Current.CancellationToken);
        await run.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken);

        Assert.InRange(elapsed.Elapsed, TimeSpan.Zero, TimeSpan.FromSeconds(5));
        Assert.Equal(WebSocketCloseStatus.PolicyViolation, socket.CloseStatus);
        var sent = socket.Sent.Select(frame => BackendToDeviceEnvelope.Parser.ParseFrom(frame)).ToList();
        Assert.Equal([PayloadCase.PairingPending, PayloadCase.Rejected], sent.Select(envelope => envelope.PayloadCase));
        Assert.Equal(ConnectionRejectReason.MalformedRequest, sent[1].Rejected.Reason);
        Assert.False(_factory.Connections.IsConnected(identity.SecretHash));
    }

    /// <summary>
    /// A peer whose next frame is always already there: every receive completes at once with
    /// <paramref name="first"/>, then <paramref name="repeated"/> forever. It never sends a close.
    /// </summary>
    private sealed class FloodingSocket(byte[] first, byte[] repeated) : WebSocket
    {
        private readonly Lock _lock = new();
        private readonly List<byte[]> _sent = [];
        private int _received;
        private WebSocketState _state = WebSocketState.Open;
        private WebSocketCloseStatus? _closeStatus;

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

        public override WebSocketCloseStatus? CloseStatus => _closeStatus;

        public override string? CloseStatusDescription => null;

        public override WebSocketState State => _state;

        public override string? SubProtocol => null;

        public override void Abort() => _state = WebSocketState.Aborted;

        public override Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken)
        {
            _closeStatus = closeStatus;
            _state = WebSocketState.Closed;
            return Task.CompletedTask;
        }

        public override Task CloseOutputAsync(WebSocketCloseStatus closeStatus, string? statusDescription, CancellationToken cancellationToken)
        {
            _closeStatus = closeStatus;
            _state = WebSocketState.CloseSent;
            return Task.CompletedTask;
        }

        public override Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken cancellationToken)
        {
            var frame = Interlocked.Increment(ref _received) == 1 ? first : repeated;
            frame.CopyTo(buffer.AsSpan());
            return Task.FromResult(new WebSocketReceiveResult(frame.Length, WebSocketMessageType.Binary, endOfMessage: true));
        }

        public override Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType, bool endOfMessage, CancellationToken cancellationToken)
        {
            lock (_lock)
            {
                _sent.Add(buffer.ToArray());
            }

            return Task.CompletedTask;
        }

        public override void Dispose()
        {
        }
    }
}
