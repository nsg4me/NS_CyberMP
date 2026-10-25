#pragma once

// Multiplayer over Steam's relay network, so nobody needs port forwarding.
//
// The game and the server keep talking their normal UDP protocol; this tunnels those datagrams through
// ISteamNetworkingMessages (the game's own steam_api64.dll), addressed by SteamID:
//
//   friend's game --UDP--> 127.0.0.1:<local proxy> ==Steam relay==> host's game --UDP--> 127.0.0.1:<server port>
//
// Host:   accepts sessions from Steam friends only and gives each one a local UDP socket towards the server.
// Friend: opens a local UDP port that the game connects to; everything sent there goes to the host's SteamID.
namespace App
{
class SteamRelay
{
public:
    static SteamRelay& Get();

    // Resolves the Steam networking exports and starts relay network access. Safe to call repeatedly.
    bool Init();
    [[nodiscard]] bool IsAvailable() const { return m_available; }
    [[nodiscard]] uint64_t GetLocalSteamId() const { return m_localSteamId; }

    // Host side: forward friends' traffic to the server on 127.0.0.1:aServerPort.
    bool StartHost(uint16_t aServerPort);

    // Friend side: returns the 127.0.0.1 port the game should connect to, or 0 when the relay cannot be used.
    uint16_t StartClient(uint64_t aHostSteamId);

    void Stop();

    // From Steam callbacks (game thread).
    void OnSessionRequest(const void* apIdentity);
    void OnSessionFailed(const void* apConnectionInfo);

private:
    SteamRelay() = default;

    void Run();
    void StartWorker();

    bool m_initTried = false;
    bool m_available = false;
    uint64_t m_localSteamId = 0;

    enum class Mode
    {
        kNone,
        kHost,
        kClient
    };

    std::mutex m_lock;
    Mode m_mode = Mode::kNone;
    uint16_t m_serverPort = 0;
    uint64_t m_hostSteamId = 0;
    uint16_t m_clientPort = 0;
    uintptr_t m_clientSocket = ~uintptr_t(0);

    std::atomic<bool> m_running{false};
    std::thread m_worker;
};
} // namespace App
