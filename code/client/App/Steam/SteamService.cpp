#include "SteamService.h"

#include "App/Settings.h"
#include "App/Network/NetworkService.h"
#include "App/Threading/ThreadService.h"
#include "App/World/NetworkWorldSystem.h"

// The game ships steam_api64.dll and initializes Steamworks itself; we only borrow its exports at runtime, so there is
// no link-time dependency and non-Steam copies (GOG, Epic) simply have no Steam features.
namespace
{
using HSteamUser = int32_t;
using ISteamFriends = void;

using GetHSteamUser_t = HSteamUser (*)();
using FindOrCreateUserInterface_t = void* (*)(HSteamUser, const char*);
using SteamFriendsAccessor_t = ISteamFriends* (*)();
using SetRichPresence_t = bool (*)(ISteamFriends*, const char*, const char*);
using ClearRichPresence_t = void (*)(ISteamFriends*);
using ActivateOverlayInviteConnect_t = void (*)(ISteamFriends*, const char*);
using ActivateOverlay_t = void (*)(ISteamFriends*, const char*);

struct CCallbackBase;
using RegisterCallback_t = void (*)(CCallbackBase*, int);
using UnregisterCallback_t = void (*)(CCallbackBase*);

struct SteamApi
{
    HMODULE module = nullptr;
    ISteamFriends* friends = nullptr;
    SetRichPresence_t setRichPresence = nullptr;
    ClearRichPresence_t clearRichPresence = nullptr;
    ActivateOverlayInviteConnect_t activateInviteConnect = nullptr;
    ActivateOverlay_t activateOverlay = nullptr;
    RegisterCallback_t registerCallback = nullptr;
    UnregisterCallback_t unregisterCallback = nullptr;
} s_steam;

template<typename T>
T Export(const char* acName)
{
    return reinterpret_cast<T>(GetProcAddress(s_steam.module, acName));
}

// GameRichPresenceJoinRequested_t (k_iSteamFriendsCallbacks + 37)
constexpr int GameRichPresenceJoinRequestedId = 337;

#pragma pack(push, 8)
struct GameRichPresenceJoinRequested
{
    uint64_t friendSteamId;
    char connect[256];
};
#pragma pack(pop)

// Same declaration as Steamworks' CCallbackBase (steam_api_common.h). Compiled by MSVC like steam_api64.dll itself,
// so the vtable (including MSVC's reversed order of the two Run overloads) matches what Steam expects.
struct CCallbackBase
{
    CCallbackBase() = default;
    virtual void Run(void* apParam) = 0;
    virtual void Run(void* apParam, bool aIOFailure, uint64_t aApiCall) = 0;
    virtual int GetCallbackSizeBytes() = 0;

    uint8_t callbackFlags = 0;
    int callbackId = 0;
};

struct JoinRequestedCallback final : CCallbackBase
{
    App::SteamService* service = nullptr;

    void Run(void* apParam) override
    {
        const auto* data = static_cast<GameRichPresenceJoinRequested*>(apParam);
        if (service && data)
        {
            char connect[257]{};
            std::memcpy(connect, data->connect, sizeof(data->connect));
            service->OnJoinRequested(data->friendSteamId, connect);
        }
    }

    void Run(void* apParam, bool, uint64_t) override
    {
        Run(apParam);
    }

    int GetCallbackSizeBytes() override
    {
        return sizeof(GameRichPresenceJoinRequested);
    }
} s_joinCallback;

std::string ConnectString()
{
    const auto& settings = Settings::Get();
    const std::string address = settings.publicAddress.c_str();
    const auto colon = address.rfind(':');
    if (colon == std::string::npos)
        return {};

    // --launcher-skip lets a friend's game skip REDprelauncher when Steam starts it from an invite, so the remaining
    // flags reach the game.
    return fmt::format("--launcher-skip --online --ip={} --port={}", address.substr(0, colon), address.substr(colon + 1));
}
} // namespace

namespace App
{
SteamService::SteamService() = default;

SteamService::~SteamService() = default;

bool SteamService::Attach()
{
    s_steam.module = GetModuleHandleW(L"steam_api64.dll");
    if (!s_steam.module)
        return false;

    // Prefer the DLL's own accessor for whichever ISteamFriends version it was built with.
    for (const auto* accessor : {"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017"})
    {
        if (const auto get = Export<SteamFriendsAccessor_t>(accessor))
        {
            s_steam.friends = get();
            if (s_steam.friends)
                break;
        }
    }

    if (!s_steam.friends)
    {
        const auto getUser = Export<GetHSteamUser_t>("SteamAPI_GetHSteamUser");
        const auto find = Export<FindOrCreateUserInterface_t>("SteamInternal_FindOrCreateUserInterface");
        if (getUser && find)
        {
            for (const auto* version : {"SteamFriends018", "SteamFriends017"})
            {
                s_steam.friends = find(getUser(), version);
                if (s_steam.friends)
                    break;
            }
        }
    }

    s_steam.setRichPresence = Export<SetRichPresence_t>("SteamAPI_ISteamFriends_SetRichPresence");
    s_steam.clearRichPresence = Export<ClearRichPresence_t>("SteamAPI_ISteamFriends_ClearRichPresence");
    s_steam.activateInviteConnect =
        Export<ActivateOverlayInviteConnect_t>("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString");
    s_steam.activateOverlay = Export<ActivateOverlay_t>("SteamAPI_ISteamFriends_ActivateGameOverlay");
    s_steam.registerCallback = Export<RegisterCallback_t>("SteamAPI_RegisterCallback");
    s_steam.unregisterCallback = Export<UnregisterCallback_t>("SteamAPI_UnregisterCallback");

    if (!s_steam.friends || !s_steam.setRichPresence)
    {
        // Loaded but not initialized yet (the game initializes Steamworks after plugins load) - try again later.
        return false;
    }

    if (s_steam.registerCallback)
    {
        s_joinCallback.service = this;
        s_joinCallback.callbackId = GameRichPresenceJoinRequestedId;
        s_steam.registerCallback(&s_joinCallback, GameRichPresenceJoinRequestedId);
    }

    spdlog::info("[Steam] attached (invite dialog {}, join requests {})",
                 s_steam.activateInviteConnect ? "yes" : "overlay only", s_steam.registerCallback ? "yes" : "no");
    return true;
}

void SteamService::OnGameUpdate(RED4ext::CGameApplication*)
{
    if (!m_attached)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextAttempt || m_attempts >= 30)
            return;

        m_nextAttempt = now + std::chrono::seconds(2);
        if (++m_attempts == 30)
            spdlog::info("[Steam] not available (not a Steam copy, or Steam is not running): invites are off");

        m_attached = Attach();
        if (!m_attached)
            return;
    }

    UpdatePresence();
}

void SteamService::UpdatePresence()
{
    const auto& settings = Settings::Get();
    if (!settings.host || m_presenceSet)
        return;

    const auto connect = ConnectString();
    if (connect.empty())
    {
        spdlog::warn("[Steam] hosting without a public address: friends cannot join through Steam");
        m_presenceSet = true;
        return;
    }

    s_steam.setRichPresence(s_steam.friends, "connect", connect.c_str());
    s_steam.setRichPresence(s_steam.friends, "status", "Hosting CyberpunkMP");
    m_presenceSet = true;
    spdlog::info("[Steam] friends can join at {}", settings.publicAddress.c_str());
}

bool SteamService::OpenInviteDialog()
{
    if (!m_attached)
        return false;

    const auto connect = ConnectString();
    if (s_steam.activateInviteConnect && !connect.empty())
        s_steam.activateInviteConnect(s_steam.friends, connect.c_str());
    else if (s_steam.activateOverlay)
        s_steam.activateOverlay(s_steam.friends, "Friends");
    else
        return false;

    return true;
}

void SteamService::OnJoinRequested(uint64_t aFriendSteamId, const char* acConnect)
{
    const std::string connect = acConnect ? acConnect : "";
    spdlog::info("[Steam] join request from {}: '{}'", aFriendSteamId, connect);

    ThreadService::RunInMainThread(
        [connect]()
        {
            if (!Settings::ApplyConnectString(connect))
            {
                spdlog::warn("[Steam] the invite has no server address");
                return;
            }

            if (Core::Container::Get<NetworkService>()->IsConnected())
            {
                spdlog::info("[Steam] already connected to a server: the invite address is used after a disconnect");
                return;
            }

            // Connect right away when a world is loaded; otherwise the address is used by the next connect.
            if (auto* system = Red::GetGameSystem<NetworkWorldSystem>())
                system->Connect();
        });
}

void SteamService::OnShutdown()
{
    if (m_attached)
    {
        if (s_steam.unregisterCallback && s_joinCallback.service)
            s_steam.unregisterCallback(&s_joinCallback);
        if (m_presenceSet && s_steam.clearRichPresence)
            s_steam.clearRichPresence(s_steam.friends);
    }
}
} // namespace App
