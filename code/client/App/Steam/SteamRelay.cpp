#include "SteamRelay.h"

#include "SteamCallback.h"

// Windows.h may already have pulled in the old winsock.h (it does unless WIN32_LEAN_AND_MEAN came first); its API
// covers everything used here, and winsock2.h would clash with it.
#ifndef _WINSOCKAPI_
#include <winsock2.h>
#endif

// Steamworks flat API, borrowed from the game's steam_api64.dll at runtime (see SteamService for the same approach).
// SteamNetworkingIdentity / SteamNetworkingMessage_t come from the GameNetworkingSockets headers, which share the
// Steamworks definitions.
namespace
{
using ISteamNetworkingMessages = void;
using ISteamNetworkingUtils = void;
using ISteamFriends = void;
using ISteamUser = void;

using Accessor_t = void* (*)();
using SendMessageToUser_t = int (*)(ISteamNetworkingMessages*, const SteamNetworkingIdentity*, const void*, uint32_t,
                                    int, int);
using ReceiveMessagesOnChannel_t = int (*)(ISteamNetworkingMessages*, int, SteamNetworkingMessage_t**, int);
using AcceptSessionWithUser_t = bool (*)(ISteamNetworkingMessages*, const SteamNetworkingIdentity*);
using CloseSessionWithUser_t = bool (*)(ISteamNetworkingMessages*, const SteamNetworkingIdentity*);
using InitRelayNetworkAccess_t = void (*)(ISteamNetworkingUtils*);
using GetRelayNetworkStatus_t = int (*)(ISteamNetworkingUtils*, void*);
using MessageRelease_t = void (*)(SteamNetworkingMessage_t*);
using GetSteamID_t = uint64_t (*)(ISteamUser*);
using GetFriendRelationship_t = int (*)(ISteamFriends*, uint64_t);
using RegisterCallback_t = void (*)(App::SteamCallbackBase*, int);

constexpr int kSessionRequestId = 1251; // SteamNetworkingMessagesSessionRequest_t
constexpr int kSessionFailedId = 1252;  // SteamNetworkingMessagesSessionFailed_t
constexpr int kChannel = 0;
constexpr int kSendFlags = 1 /* NoNagle (unreliable) */ | 32 /* AutoRestartBrokenSession */;
constexpr int kFriendRelationshipFriend = 3;
constexpr int kAvailabilityCurrent = 100;

struct
{
    HMODULE module = nullptr;
    ISteamNetworkingMessages* messages = nullptr;
    ISteamNetworkingUtils* utils = nullptr;
    ISteamFriends* friends = nullptr;
    SendMessageToUser_t send = nullptr;
    ReceiveMessagesOnChannel_t receive = nullptr;
    AcceptSessionWithUser_t accept = nullptr;
    CloseSessionWithUser_t close = nullptr;
    InitRelayNetworkAccess_t initRelay = nullptr;
    GetRelayNetworkStatus_t relayStatus = nullptr;
    MessageRelease_t release = nullptr;
    GetFriendRelationship_t relationship = nullptr;
    RegisterCallback_t registerCallback = nullptr;
} s_api;

template<typename T>
T Export(const char* acName)
{
    return reinterpret_cast<T>(GetProcAddress(s_api.module, acName));
}

void* FirstInterface(std::initializer_list<const char*> aAccessors)
{
    for (const auto* name : aAccessors)
    {
        if (const auto get = Export<Accessor_t>(name))
        {
            if (auto* iface = get())
                return iface;
        }
    }
    return nullptr;
}

void Release(SteamNetworkingMessage_t* apMessage)
{
    if (s_api.release)
        s_api.release(apMessage);
    else if (apMessage->m_pfnRelease)
        apMessage->m_pfnRelease(apMessage);
}

SteamNetworkingIdentity Identity(uint64_t aSteamId)
{
    SteamNetworkingIdentity identity{};
    identity.SetSteamID64(aSteamId);
    return identity;
}

#pragma pack(push, 8)
struct SessionRequest
{
    SteamNetworkingIdentity identityRemote;
};
#pragma pack(pop)

struct SessionRequestCallback final : App::SteamCallbackBase
{
    void Run(void* apParam) override { App::SteamRelay::Get().OnSessionRequest(apParam); }
    void Run(void* apParam, bool, uint64_t) override { Run(apParam); }
    int GetCallbackSizeBytes() override { return sizeof(SessionRequest); }
} s_sessionRequest;

struct SessionFailedCallback final : App::SteamCallbackBase
{
    void Run(void* apParam) override { App::SteamRelay::Get().OnSessionFailed(apParam); }
    void Run(void* apParam, bool, uint64_t) override { Run(apParam); }
    // SteamNetConnectionInfo_t; only its leading identity is read.
    int GetCallbackSizeBytes() override { return 696; }
} s_sessionFailed;

SOCKET MakeLoopbackSocket(uint16_t aConnectPort)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
    {
        closesocket(s);
        return INVALID_SOCKET;
    }

    if (aConnectPort)
    {
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        server.sin_port = htons(aConnectPort);
        if (connect(s, reinterpret_cast<sockaddr*>(&server), sizeof(server)) != 0)
        {
            closesocket(s);
            return INVALID_SOCKET;
        }
    }

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    return s;
}

uint16_t LocalPort(SOCKET aSocket)
{
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (getsockname(aSocket, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
        return 0;
    return ntohs(addr.sin_port);
}
} // namespace

namespace App
{
SteamRelay& SteamRelay::Get()
{
    static SteamRelay s_instance;
    return s_instance;
}

bool SteamRelay::Init()
{
    if (m_available)
        return true;

    s_api.module = GetModuleHandleW(L"steam_api64.dll");
    if (!s_api.module)
        return false;

    s_api.messages = FirstInterface({"SteamAPI_SteamNetworkingMessages_SteamAPI_v002"});
    s_api.utils = FirstInterface({"SteamAPI_SteamNetworkingUtils_SteamAPI_v004", "SteamAPI_SteamNetworkingUtils_SteamAPI_v003"});
    s_api.friends = FirstInterface({"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017"});
    auto* user = FirstInterface({"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021",
                                 "SteamAPI_SteamUser_v020"});

    s_api.send = Export<SendMessageToUser_t>("SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    s_api.receive = Export<ReceiveMessagesOnChannel_t>("SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    s_api.accept = Export<AcceptSessionWithUser_t>("SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    s_api.close = Export<CloseSessionWithUser_t>("SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    s_api.initRelay = Export<InitRelayNetworkAccess_t>("SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    s_api.relayStatus = Export<GetRelayNetworkStatus_t>("SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus");
    s_api.release = Export<MessageRelease_t>("SteamAPI_SteamNetworkingMessage_t_Release");
    s_api.relationship = Export<GetFriendRelationship_t>("SteamAPI_ISteamFriends_GetFriendRelationship");
    s_api.registerCallback = Export<RegisterCallback_t>("SteamAPI_RegisterCallback");
    const auto getSteamId = Export<GetSteamID_t>("SteamAPI_ISteamUser_GetSteamID");

    const bool ok = s_api.messages && s_api.send && s_api.receive && s_api.accept && user && getSteamId;
    if (!ok)
    {
        if (!m_initTried)
            spdlog::warn("[Relay] Steam networking is not available in this steam_api64.dll (messages {}, send {}, "
                         "receive {}, accept {}, user {}): direct connections only",
                         s_api.messages != nullptr, s_api.send != nullptr, s_api.receive != nullptr,
                         s_api.accept != nullptr, user != nullptr);
        m_initTried = true;
        return false;
    }

    m_localSteamId = getSteamId(user);
    if (m_localSteamId == 0)
        return false; // Steam not logged in yet; try again later

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (s_api.utils && s_api.initRelay)
        s_api.initRelay(s_api.utils);

    if (s_api.registerCallback)
    {
        s_sessionRequest.callbackId = kSessionRequestId;
        s_api.registerCallback(&s_sessionRequest, kSessionRequestId);
        s_sessionFailed.callbackId = kSessionFailedId;
        s_api.registerCallback(&s_sessionFailed, kSessionFailedId);
    }

    m_available = true;
    m_initTried = true;
    spdlog::info("[Relay] Steam networking ready (SteamID {}, session callbacks {})", m_localSteamId,
                 s_api.registerCallback ? "yes" : "no");
    return true;
}

bool SteamRelay::StartHost(uint16_t aServerPort)
{
    if (!Init())
        return false;

    {
        std::lock_guard _(m_lock);
        m_mode = Mode::kHost;
        m_serverPort = aServerPort;
    }

    StartWorker();
    spdlog::info("[Relay] hosting: Steam friends are forwarded to 127.0.0.1:{}", aServerPort);
    return true;
}

uint16_t SteamRelay::StartClient(uint64_t aHostSteamId)
{
    if (!Init())
        return 0;

    uint16_t port = 0;
    {
        std::lock_guard _(m_lock);

        if (m_mode == Mode::kClient && m_hostSteamId == aHostSteamId && m_clientPort)
            return m_clientPort;

        if (m_clientSocket != ~uintptr_t(0))
        {
            closesocket(static_cast<SOCKET>(m_clientSocket));
            m_clientSocket = ~uintptr_t(0);
            m_clientPort = 0;
        }

        const SOCKET s = MakeLoopbackSocket(0);
        if (s == INVALID_SOCKET)
        {
            spdlog::error("[Relay] cannot open the local relay port");
            return 0;
        }

        m_clientSocket = static_cast<uintptr_t>(s);
        m_clientPort = LocalPort(s);
        m_hostSteamId = aHostSteamId;
        m_mode = Mode::kClient;
        port = m_clientPort;
    }

    spdlog::info("[Relay] joining {} through Steam (game connects to 127.0.0.1:{})", aHostSteamId, port);
    StartWorker();
    return port;
}

void SteamRelay::StartWorker()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return;

    m_worker = std::thread([this] { Run(); });
    m_worker.detach();
}

void SteamRelay::Stop()
{
    m_running = false;
}

void SteamRelay::OnSessionRequest(const void* apIdentity)
{
    const auto& identity = static_cast<const SessionRequest*>(apIdentity)->identityRemote;
    const uint64_t steamId = identity.GetSteamID64();

    Mode mode;
    {
        std::lock_guard _(m_lock);
        mode = m_mode;
    }

    if (mode != Mode::kHost)
    {
        spdlog::info("[Relay] ignoring session request from {} (not hosting)", steamId);
        return;
    }

    // Only Steam friends may join: the SteamID is visible in rich presence, the server has no password.
    if (s_api.relationship && s_api.friends &&
        s_api.relationship(s_api.friends, steamId) != kFriendRelationshipFriend)
    {
        spdlog::warn("[Relay] refused session from {}: not on your Steam friends list", steamId);
        return;
    }

    const bool accepted = s_api.accept(s_api.messages, &identity);
    spdlog::info("[Relay] {} session from {}", accepted ? "accepted" : "could not accept", steamId);
}

void SteamRelay::OnSessionFailed(const void* apConnectionInfo)
{
    const auto* identity = static_cast<const SteamNetworkingIdentity*>(apConnectionInfo);
    spdlog::warn("[Relay] Steam session with {} failed", identity->GetSteamID64());
}

void SteamRelay::Run()
{
    struct Peer
    {
        SteamNetworkingIdentity identity;
        SOCKET socket = INVALID_SOCKET;
        std::chrono::steady_clock::time_point lastSeen;
    };

    std::unordered_map<uint64_t, Peer> peers;
    sockaddr_in gameAddress{};
    bool haveGameAddress = false;
    char buffer[2048];
    SteamNetworkingMessage_t* messages[64];

    auto nextStatus = std::chrono::steady_clock::now();
    int lastAvailability = -1000;

    while (m_running)
    {
        Mode mode;
        uint16_t serverPort;
        uint64_t hostSteamId;
        SOCKET clientSocket;
        {
            std::lock_guard _(m_lock);
            mode = m_mode;
            serverPort = m_serverPort;
            hostSteamId = m_hostSteamId;
            clientSocket = static_cast<SOCKET>(m_clientSocket);
        }

        const auto now = std::chrono::steady_clock::now();

        // Relay network status, logged when it changes (useful when friends cannot connect).
        if (s_api.utils && s_api.relayStatus && now >= nextStatus)
        {
            nextStatus = now + std::chrono::seconds(5);
            char details[512]{};
            const int availability = s_api.relayStatus(s_api.utils, details);
            if (availability != lastAvailability)
            {
                lastAvailability = availability;
                const char* debug = details + 16; // SteamRelayNetworkStatus_t::m_debugMsg
                spdlog::info("[Relay] Steam relay network: {} {}",
                             availability == kAvailabilityCurrent ? "ready" : std::to_string(availability), debug);
            }
        }

        // Steam -> local UDP
        const int count = s_api.receive(s_api.messages, kChannel, messages, 64);
        for (int i = 0; i < count; ++i)
        {
            auto* message = messages[i];
            const uint64_t from = message->m_identityPeer.GetSteamID64();

            if (mode == Mode::kHost)
            {
                auto it = peers.find(from);
                if (it == peers.end())
                {
                    Peer peer;
                    peer.identity = message->m_identityPeer;
                    peer.socket = MakeLoopbackSocket(serverPort);
                    if (peer.socket == INVALID_SOCKET)
                    {
                        spdlog::error("[Relay] cannot open a server socket for {}", from);
                        Release(message);
                        continue;
                    }
                    spdlog::info("[Relay] {} connected through Steam", from);
                    it = peers.emplace(from, peer).first;
                }
                it->second.lastSeen = now;
                send(it->second.socket, static_cast<const char*>(message->m_pData), message->m_cbSize, 0);
            }
            else if (mode == Mode::kClient && from == hostSteamId && haveGameAddress)
            {
                sendto(clientSocket, static_cast<const char*>(message->m_pData), message->m_cbSize, 0,
                       reinterpret_cast<sockaddr*>(&gameAddress), sizeof(gameAddress));
            }

            Release(message);
        }

        // Local UDP -> Steam
        if (mode == Mode::kHost)
        {
            for (auto it = peers.begin(); it != peers.end();)
            {
                auto& peer = it->second;
                int received;
                while ((received = recv(peer.socket, buffer, sizeof(buffer), 0)) > 0)
                    s_api.send(s_api.messages, &peer.identity, buffer, received, kSendFlags, kChannel);

                if (now - peer.lastSeen > std::chrono::minutes(2))
                {
                    spdlog::info("[Relay] {} timed out", peer.identity.GetSteamID64());
                    closesocket(peer.socket);
                    if (s_api.close)
                        s_api.close(s_api.messages, &peer.identity);
                    it = peers.erase(it);
                }
                else
                    ++it;
            }
        }
        else if (mode == Mode::kClient && clientSocket != INVALID_SOCKET)
        {
            const auto host = Identity(hostSteamId);
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int received;
            while ((received = recvfrom(clientSocket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from),
                                        &fromLen)) > 0)
            {
                gameAddress = from;
                haveGameAddress = true;
                s_api.send(s_api.messages, &host, buffer, received, kSendFlags, kChannel);
                fromLen = sizeof(from);
            }
        }

        // Wait briefly for local traffic (Steam's queue is polled every pass, so latency stays around 1 ms).
        fd_set readable;
        FD_ZERO(&readable);
        int sockets = 0;
        if (mode == Mode::kClient && clientSocket != INVALID_SOCKET)
        {
            FD_SET(clientSocket, &readable);
            ++sockets;
        }
        for (auto& [id, peer] : peers)
        {
            if (sockets >= FD_SETSIZE)
                break;
            FD_SET(peer.socket, &readable);
            ++sockets;
        }

        if (sockets > 0)
        {
            timeval timeout{0, 1000};
            select(0, &readable, nullptr, nullptr, &timeout);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    for (auto& [id, peer] : peers)
        closesocket(peer.socket);
}
} // namespace App
