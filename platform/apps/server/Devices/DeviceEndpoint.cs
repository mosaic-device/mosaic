using System.Net.WebSockets;
using Google.Protobuf;
using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>
/// <c>/ws/device</c>: the one entry point for first pairing and every reconnect (Device
/// Connection Protocol). A paired credential is authenticated; an unknown one gets a pairing
/// code; a revoked, malformed or unsupported one is rejected before any device access.
/// </summary>
public static class DeviceEndpoint
{
    public const string Path = "/ws/device";

    // Fixed frame limits, part of protocol version 1 (Device Connection Protocol, section 1). They
    // leave room above the generated nanopb sizes, so a field added later still reaches the
    // decoder and is skipped as unknown. They never go down.

    /// <summary>The largest frame the server accepts from a device.</summary>
    public const int MaxDeviceFrameBytes = 512;

    /// <summary>The largest frame a device accepts; the server never sends more.</summary>
    public const int MaxServerFrameBytes = 4096;

    public static void MapDeviceEndpoint(this WebApplication app)
    {
        var options = app.Services.GetRequiredService<DeviceConnectionOptions>();
        var handler = app.Services.GetRequiredService<DeviceConnectionHandler>();
        app.Map(Path, async context =>
        {
            // A browser always sends Origin; the device never does. This stops a web page from
            // opening device connections and using up pending pairing slots (section 1).
            if (context.Request.Headers.Origin.Count > 0)
            {
                context.Response.StatusCode = StatusCodes.Status403Forbidden;
                return;
            }

            if (!context.WebSockets.IsWebSocketRequest)
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }

            using var socket = await context.WebSockets.AcceptWebSocketAsync(new WebSocketAcceptContext
            {
                DangerousEnableCompression = false,
                KeepAliveInterval = options.KeepAliveInterval,
                KeepAliveTimeout = options.KeepAliveTimeout,
            });
            await handler.RunAsync(socket);
        });
    }
}

/// <summary>Runs one device connection from its first frame until it closes.</summary>
public sealed partial class DeviceConnectionHandler(
    DeviceConnectionOptions options,
    DevicePairing pairing,
    PairingCodes codes,
    DeviceConnections connections,
    TimeProvider time,
    IHostApplicationLifetime lifetime,
    ILogger<DeviceConnectionHandler> logger)
{
    // How long to wait for the device's close reply after the server closes. Real time, so a
    // test clock cannot hold a socket open.
    private static readonly TimeSpan CloseGrace = TimeSpan.FromSeconds(1);

    private static readonly Task<DeviceSignal> NoSignal = new TaskCompletionSource<DeviceSignal>().Task;

    // Completes when the server starts shutting down. One for every connection: a wait made per
    // connection would stay registered on the shutdown token after its connection ended.
    private readonly Task _stopping = Task.Delay(Timeout.Infinite, lifetime.ApplicationStopping);

    private enum FrameKind
    {
        Binary,
        Text,
        TooLarge,
        Closed,
    }

    private readonly record struct Frame(FrameKind Kind, byte[] Bytes);

    /// <summary>The socket and its one outstanding receive, which every step waits on together.</summary>
    private sealed class Link(WebSocket socket)
    {
        public WebSocket Socket { get; } = socket;

        public Task<Frame> Receive { get; private set; } = ReceiveAsync(socket);

        /// <summary>Starts reading the next frame once the current one has been handled.</summary>
        public void ReceiveNext() => Receive = ReceiveAsync(Socket);
    }

    public async Task RunAsync(WebSocket socket)
    {
        var link = new Link(socket);
        try
        {
            await RunAsync(link, _stopping);
        }
        catch (WebSocketException)
        {
            // The device went away, or missed its pongs and the socket was aborted.
        }
        catch (Exception exception)
        {
            LogUnexpectedFailure(exception);
            await CloseAsync(link, WebSocketCloseStatus.InternalServerError);
        }
        finally
        {
            // Observe the last receive so its failure after close is never unobserved.
            _ = link.Receive.ContinueWith(static task => _ = task.Exception, TaskScheduler.Default);
        }
    }

    private async Task RunAsync(Link link, Task stopping)
    {
        using var handshake = new CancellationTokenSource();
        var deadline = Task.Delay(options.HandshakeTimeout, time, handshake.Token);
        var first = await Task.WhenAny(link.Receive, deadline, stopping);
        await handshake.CancelAsync();
        if (first == stopping)
        {
            await CloseAsync(link, WebSocketCloseStatus.EndpointUnavailable);
            return;
        }

        if (first == deadline)
        {
            await CloseAsync(link, WebSocketCloseStatus.PolicyViolation);
            return;
        }

        var frame = await link.Receive;
        if (!await AcceptsFrameKind(link, frame))
        {
            return;
        }

        link.ReceiveNext();
        DeviceToBackendEnvelope envelope;
        try
        {
            envelope = DeviceToBackendEnvelope.Parser.ParseFrom(frame.Bytes);
        }
        catch (InvalidProtocolBufferException)
        {
            await RejectAsync(link, ConnectionRejectReason.MalformedRequest);
            return;
        }

        var check = ConnectCheck.Check(envelope);
        if (check.Connect is not { } connect)
        {
            LogRejected(check.Rejection!.Value);
            await RejectAsync(link, check.Rejection!.Value);
            return;
        }

        // Version and fields are checked; now the credential (section 5). The connection is
        // registered first, so a claim that commits after the credential is read can reach it.
        var connection = connections.Register(connect.SecretHash);
        try
        {
            var admission = pairing.Admit(connect.SecretHash);
            var credential = admission.Credential;
            if (credential.State == CredentialState.Revoked)
            {
                LogRevokedCredential(credential.DeviceId!.Value);
                await RejectAsync(link, ConnectionRejectReason.CredentialRevoked);
                return;
            }

            var code = admission.Code;
            if (credential.State == CredentialState.Paired)
            {
                LogPaired(credential.DeviceId!.Value);
                await SendPairedAsync(link, connection);
            }
            else
            {
                if (code is null)
                {
                    LogPendingLimit(options.MaxPendingPairings);
                    await RejectAsync(link, ConnectionRejectReason.ServerBusy);
                    return;
                }

                LogPending();
                await SendAsync(link, Pending(code));
            }

            await ConnectedAsync(link, stopping, connection, connect, code);
        }
        finally
        {
            connections.Unregister(connection);
        }
    }

    /// <summary>
    /// After the handshake reply. While <paramref name="code"/> is set the device is pending:
    /// any frame from it is a protocol error, and its code is replaced at expiry. Once paired, it
    /// is sent notifications handed to its connection, and their receipts are matched; other
    /// frames this server doesn't handle yet are ignored (section 7).
    /// </summary>
    private async Task ConnectedAsync(
        Link link, Task stopping, DeviceConnection connection, ValidConnect connect, PendingCode? code)
    {
        var signal = connection.NextSignalAsync(CancellationToken.None).AsTask();
        while (true)
        {
            using var expiryCancel = new CancellationTokenSource();
            var expiry = code is null
                ? NoSignal
                : Task.Delay(Until(code), time, expiryCancel.Token);
            await Task.WhenAny(link.Receive, signal, expiry, stopping);
            await expiryCancel.CancelAsync();

            // WhenAny favours whichever ready task is listed first, which would let a steady
            // stream of frames hold off a signal or shutdown. Choose by priority instead.
            var next = WakeOrder.Next(stopping.IsCompleted, signal.IsCompleted, expiry.IsCompletedSuccessfully);
            if (next == Wake.Stopping)
            {
                await CloseAsync(link, WebSocketCloseStatus.EndpointUnavailable);
                return;
            }

            if (next == Wake.Frame)
            {
                var frame = await link.Receive;
                if (!await AcceptsFrameKind(link, frame))
                {
                    return;
                }

                link.ReceiveNext();
                if (code is not null || ReadAfterPairing(frame) is not { } received)
                {
                    await RejectAsync(link, ConnectionRejectReason.MalformedRequest);
                    return;
                }

                if (received.PayloadCase == DeviceToBackendEnvelope.PayloadOneofCase.NotificationReceipt)
                {
                    connection.Resolve(received.NotificationReceipt);
                }

                continue;
            }

            if (next == Wake.Signal)
            {
                switch (await signal)
                {
                    case DeviceSignal.Paired when code is not null:
                        code = null;
                        await SendPairedAsync(link, connection);
                        break;
                    case DeviceSignal.Deliver when connection.TakeToSend() is { } delivery:
                        await SendAsync(link, new BackendToDeviceEnvelope { Notification = delivery.Notification });
                        break;
                    case DeviceSignal.Revoked:
                        await RejectAsync(link, ConnectionRejectReason.CredentialRevoked);
                        return;
                    case DeviceSignal.Replaced:
                        await CloseAsync(link, WebSocketCloseStatus.NormalClosure);
                        return;
                }

                signal = connection.NextSignalAsync(CancellationToken.None).AsTask();
                continue;
            }

            // The code expired while the device was connected. A claim may have just committed.
            var admission = pairing.Admit(connect.SecretHash);
            switch (admission.Credential.State)
            {
                case CredentialState.Paired:
                    code = null;
                    await SendPairedAsync(link, connection);
                    break;
                case CredentialState.Revoked:
                    await RejectAsync(link, ConnectionRejectReason.CredentialRevoked);
                    return;
                default:
                    code = admission.Code;
                    if (code is null)
                    {
                        LogPendingLimit(options.MaxPendingPairings);
                        await RejectAsync(link, ConnectionRejectReason.ServerBusy);
                        return;
                    }

                    await SendAsync(link, Pending(code));
                    break;
            }
        }
    }

    /// <summary>Closes for a text frame (1003), an oversized one (1009) or the device's own close.</summary>
    private static async Task<bool> AcceptsFrameKind(Link link, Frame frame)
    {
        switch (frame.Kind)
        {
            case FrameKind.Binary:
                return true;
            case FrameKind.Text:
                await CloseAsync(link, WebSocketCloseStatus.InvalidMessageType);
                return false;
            case FrameKind.TooLarge:
                await CloseAsync(link, WebSocketCloseStatus.MessageTooBig);
                return false;
            default:
                await CloseAsync(link, WebSocketCloseStatus.NormalClosure);
                return false;
        }
    }

    /// <summary>
    /// After <c>paired</c>, any decodable frame other than a second <c>connect</c> is accepted:
    /// a receipt is matched to its notification and anything this server doesn't handle yet is
    /// ignored (section 7). Null means the frame is a protocol error.
    /// </summary>
    private static DeviceToBackendEnvelope? ReadAfterPairing(Frame frame)
    {
        try
        {
            var envelope = DeviceToBackendEnvelope.Parser.ParseFrom(frame.Bytes);
            return envelope.PayloadCase == DeviceToBackendEnvelope.PayloadOneofCase.Connect ? null : envelope;
        }
        catch (InvalidProtocolBufferException)
        {
            return null;
        }
    }

    /// <summary>Sends <c>paired</c>; from then on the connection may be sent notifications.</summary>
    private static async Task SendPairedAsync(Link link, DeviceConnection connection)
    {
        await SendAsync(link, new BackendToDeviceEnvelope { Paired = new DevicePaired() });
        connection.MarkReady();
    }

    private TimeSpan Until(PendingCode code)
    {
        // PairingCodes treats a code with under a second left as expired.
        var remaining = code.ExpiresAt - time.GetUtcNow() - TimeSpan.FromSeconds(1);
        return remaining > TimeSpan.Zero ? remaining : TimeSpan.Zero;
    }

    private BackendToDeviceEnvelope Pending(PendingCode code) => new()
    {
        PairingPending = new PairingPending { Code = code.Code, ExpiresInSeconds = (uint)codes.SecondsLeft(code) },
    };

    /// <summary>Sends <c>rejected</c>, then closes: 1000 after a revocation, 1008 otherwise (section 5).</summary>
    private static async Task RejectAsync(Link link, ConnectionRejectReason reason)
    {
        await SendAsync(link, new BackendToDeviceEnvelope { Rejected = new ConnectionRejected { Reason = reason } });
        await CloseAsync(
            link,
            reason == ConnectionRejectReason.CredentialRevoked ? WebSocketCloseStatus.NormalClosure : WebSocketCloseStatus.PolicyViolation);
    }

    private static async Task SendAsync(Link link, BackendToDeviceEnvelope envelope)
    {
        var bytes = envelope.ToByteArray();
        if (bytes.Length > DeviceEndpoint.MaxServerFrameBytes)
        {
            throw new InvalidOperationException($"A server frame of {bytes.Length} bytes exceeds the device's limit.");
        }

        await link.Socket.SendAsync(bytes, WebSocketMessageType.Binary, endOfMessage: true, CancellationToken.None);
    }

    private static async Task CloseAsync(Link link, WebSocketCloseStatus status)
    {
        if (link.Socket.State is not (WebSocketState.Open or WebSocketState.CloseReceived))
        {
            return;
        }

        try
        {
            await link.Socket.CloseOutputAsync(status, null, CancellationToken.None);

            // Give the device a moment to answer, so the close frame isn't lost to an abort. Frames
            // it sent before seeing the close are read and dropped until its close arrives: closing
            // with unread data resets the connection, which can discard what was just sent to it.
            // The deadline is checked before each frame, since WhenAny would favour a frame that is
            // already waiting and a device that never stops sending would hold the socket open.
            var grace = Task.Delay(CloseGrace);
            while (true)
            {
                await Task.WhenAny(link.Receive, grace);
                if (grace.IsCompleted || (await link.Receive).Kind == FrameKind.Closed)
                {
                    break;
                }

                link.ReceiveNext();
            }
        }
        catch (WebSocketException)
        {
            // Already gone.
        }
    }

    /// <summary>
    /// Reads one whole message, up to <see cref="DeviceEndpoint.MaxDeviceFrameBytes"/>. Not
    /// cancellable: cancelling a WebSocket receive aborts the socket, and the server must still
    /// be able to send its close code.
    /// </summary>
    private static async Task<Frame> ReceiveAsync(WebSocket socket)
    {
        var buffer = new byte[DeviceEndpoint.MaxDeviceFrameBytes + 1];
        var count = 0;
        while (true)
        {
            if (count == buffer.Length)
            {
                return new Frame(FrameKind.TooLarge, []);
            }

            var result = await socket.ReceiveAsync(buffer.AsMemory(count), CancellationToken.None);
            switch (result.MessageType)
            {
                case WebSocketMessageType.Close:
                    return new Frame(FrameKind.Closed, []);
                case WebSocketMessageType.Text:
                    return new Frame(FrameKind.Text, []);
            }

            count += result.Count;
            if (count > DeviceEndpoint.MaxDeviceFrameBytes)
            {
                return new Frame(FrameKind.TooLarge, []);
            }

            if (result.EndOfMessage)
            {
                return new Frame(FrameKind.Binary, buffer[..count]);
            }
        }
    }

    // Secrets and pairing codes are never logged.
    [LoggerMessage(Level = LogLevel.Information, Message = "Paired device {DeviceId} connected")]
    private partial void LogPaired(long deviceId);

    [LoggerMessage(Level = LogLevel.Information, Message = "An unpaired device is waiting to be claimed")]
    private partial void LogPending();

    [LoggerMessage(Level = LogLevel.Information, Message = "Rejected a device connection: {Reason}")]
    private partial void LogRejected(ConnectionRejectReason reason);

    [LoggerMessage(Level = LogLevel.Information, Message = "Rejected removed device {DeviceId}")]
    private partial void LogRevokedCredential(long deviceId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Rejected an unpaired device: {Limit} pairing codes are already live")]
    private partial void LogPendingLimit(int limit);

    [LoggerMessage(Level = LogLevel.Error, Message = "Device connection failed unexpectedly")]
    private partial void LogUnexpectedFailure(Exception exception);
}
