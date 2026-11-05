using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Security.Cryptography;
using Google.Protobuf;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Time.Testing;
using Mosaic.Device.V1;
using Mosaic.Server.Data;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// Hosts the real server on Kestrel, so tests connect over a real WebSocket, with a fake clock
/// for code expiry and the handshake deadline, and a log capture to check what is logged.
/// </summary>
public sealed class DeviceApiFactory : WebApplicationFactory<Program>
{
    public const string SetupCode = "K7QF-2M9X";

    private readonly string _directory;
    private readonly bool _ownsDirectory;
    private readonly Dictionary<string, string?> _settings;

    public DeviceApiFactory(Action<Dictionary<string, string?>>? configure = null, string? directory = null)
    {
        _ownsDirectory = directory is null;
        _directory = directory ?? Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));
        _settings = new()
        {
            [MosaicDatabase.PathKey] = Path.Combine(_directory, "mosaic.db"),
            ["Auth:PublicOrigin"] = "https://mosaic.test",
            ["MOSAIC_SETUP_CODE"] = SetupCode,
        };
        configure?.Invoke(_settings);
        UseKestrel(0);
    }

    public string Directory => _directory;

    public FakeTimeProvider Time { get; } = new(new DateTimeOffset(2026, 11, 4, 12, 0, 0, TimeSpan.Zero));

    public ConcurrentQueue<string> Logs { get; } = new();

    public DeviceStore Devices => Services.GetRequiredService<DeviceStore>();

    public PairingCodes Codes => Services.GetRequiredService<PairingCodes>();

    public DeviceConnections Connections => Services.GetRequiredService<DeviceConnections>();

    public Uri BaseAddress
    {
        get
        {
            StartServer();
            var address = Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.First();
            return new Uri(address);
        }
    }

    public Uri DeviceUri => new UriBuilder(BaseAddress) { Scheme = "ws", Path = DeviceEndpoint.Path }.Uri;

    public async Task<FakeDevice> ConnectAsync(TestDevice? device = null)
    {
        var socket = new ClientWebSocket();
        await socket.ConnectAsync(DeviceUri, TestContext.Current.CancellationToken);
        return new FakeDevice(socket, device ?? TestDevice.Create());
    }

    /// <summary>Records <paramref name="device"/> as paired, as a completed claim would.</summary>
    public long Pair(TestDevice device) => Devices.Pair(device.SecretHash)!.Value;

    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        builder.UseEnvironment("Production");
        foreach (var (key, value) in _settings)
        {
            builder.UseSetting(key, value);
        }

        builder.ConfigureServices(services =>
        {
            services.AddSingleton<TimeProvider>(Time);
            services.AddLogging(logging => logging.AddProvider(new CaptureLoggerProvider(Logs)));
        });
    }

    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing && _ownsDirectory && System.IO.Directory.Exists(_directory))
        {
            System.IO.Directory.Delete(_directory, recursive: true);
        }
    }

    private sealed class CaptureLoggerProvider(ConcurrentQueue<string> logs) : ILoggerProvider
    {
        public ILogger CreateLogger(string categoryName) => new CaptureLogger(logs);

        public void Dispose()
        {
        }
    }

    private sealed class CaptureLogger(ConcurrentQueue<string> logs) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state)
            where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter) =>
            logs.Enqueue(formatter(state, exception) + exception);
    }
}

/// <summary>A device's identity as the firmware would hold it: a random 32-byte secret.</summary>
public sealed record TestDevice(byte[] Secret, byte[] SecretHash)
{
    public static TestDevice Create()
    {
        var secret = RandomNumberGenerator.GetBytes(32);
        return new TestDevice(secret, SHA256.HashData(secret));
    }

    public DeviceToBackendEnvelope Connect(uint version = 1) => new()
    {
        Connect = new DeviceConnect { ProtocolVersion = version, AuthSecret = ByteString.CopyFrom(Secret) },
    };
}

/// <summary>A fake device client speaking the protocol over a real WebSocket.</summary>
public sealed class FakeDevice(ClientWebSocket socket, TestDevice device) : IAsyncDisposable
{
    private static readonly TimeSpan Wait = TimeSpan.FromSeconds(10);

    public TestDevice Device { get; } = device;

    public ClientWebSocket Socket { get; } = socket;

    public Task SendAsync(IMessage message) => SendRawAsync(message.ToByteArray());

    public Task SendRawAsync(byte[] bytes, WebSocketMessageType type = WebSocketMessageType.Binary) =>
        Socket.SendAsync(bytes, type, endOfMessage: true, TestContext.Current.CancellationToken);

    /// <summary>Sends <c>connect</c> and returns the server's reply.</summary>
    public async Task<BackendToDeviceEnvelope> HandshakeAsync()
    {
        await SendAsync(Device.Connect());
        return await ReceiveAsync();
    }

    public async Task<BackendToDeviceEnvelope> ReceiveAsync()
    {
        var (type, bytes) = await ReceiveMessageAsync();
        Assert.Equal(WebSocketMessageType.Binary, type);
        Assert.True(bytes.Length <= DeviceEndpoint.MaxServerFrameBytes);
        return BackendToDeviceEnvelope.Parser.ParseFrom(bytes);
    }

    /// <summary>Expects the server to close next, and returns its close code.</summary>
    public async Task<WebSocketCloseStatus?> ReceiveCloseAsync()
    {
        var (type, _) = await ReceiveMessageAsync();
        Assert.Equal(WebSocketMessageType.Close, type);
        return Socket.CloseStatus;
    }

    /// <summary>Expects <c>rejected</c> with <paramref name="reason"/>, then a close.</summary>
    public async Task<WebSocketCloseStatus?> ExpectRejectedAsync(ConnectionRejectReason reason)
    {
        var reply = await ReceiveAsync();
        Assert.Equal(BackendToDeviceEnvelope.PayloadOneofCase.Rejected, reply.PayloadCase);
        Assert.Equal(reason, reply.Rejected.Reason);
        return await ReceiveCloseAsync();
    }

    public async ValueTask DisposeAsync()
    {
        if (Socket.State == WebSocketState.Open)
        {
            try
            {
                await Socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
            }
            catch (WebSocketException)
            {
            }
        }

        Socket.Dispose();
    }

    private async Task<(WebSocketMessageType Type, byte[] Bytes)> ReceiveMessageAsync()
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(Wait);
        var buffer = new byte[4096];
        var count = 0;
        while (true)
        {
            var result = await Socket.ReceiveAsync(buffer.AsMemory(count), timeout.Token);
            count += result.Count;
            if (result.EndOfMessage || result.MessageType == WebSocketMessageType.Close)
            {
                return (result.MessageType, buffer[..count]);
            }
        }
    }
}
