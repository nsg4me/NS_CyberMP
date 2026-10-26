#include "SteamLobby.h"

#include <fstream>

#include "SteamCallback.h"
#include "App/Settings.h"
#include "App/Rendering/Overlay.h"
#include "App/Network/NetworkService.h"
#include "App/World/NetworkWorldSystem.h"

extern std::filesystem::path GCyberpunkMpLocation;

namespace
{
using ISteamMatchmaking = void;
using ISteamFriends = void;
using ISteamUtils = void;
using ISteamUser = void;
using Accessor_t = void* (*)();

struct
{
    HMODULE module = nullptr;
    ISteamMatchmaking* mm = nullptr;
    ISteamFriends* friends = nullptr;
    ISteamUtils* utils = nullptr;

    uint64_t (*createLobby)(ISteamMatchmaking*, int, int) = nullptr;
    uint64_t (*joinLobby)(ISteamMatchmaking*, uint64_t) = nullptr;
    void (*leaveLobby)(ISteamMatchmaking*, uint64_t) = nullptr;
    int (*numMembers)(ISteamMatchmaking*, uint64_t) = nullptr;
    uint64_t (*memberByIndex)(ISteamMatchmaking*, uint64_t, int) = nullptr;
    const char* (*getData)(ISteamMatchmaking*, uint64_t, const char*) = nullptr;
    bool (*setData)(ISteamMatchmaking*, uint64_t, const char*, const char*) = nullptr;
    const char* (*getMemberData)(ISteamMatchmaking*, uint64_t, uint64_t, const char*) = nullptr;
    void (*setMemberData)(ISteamMatchmaking*, uint64_t, const char*, const char*) = nullptr;
    uint64_t (*owner)(ISteamMatchmaking*, uint64_t) = nullptr;

    const char* (*personaName)(ISteamFriends*, uint64_t) = nullptr;
    int (*mediumAvatar)(ISteamFriends*, uint64_t) = nullptr;
    bool (*setRichPresence)(ISteamFriends*, const char*, const char*) = nullptr;

    bool (*imageSize)(ISteamUtils*, int, uint32_t*, uint32_t*) = nullptr;
    bool (*imageRgba)(ISteamUtils*, int, uint8_t*, int) = nullptr;

    void (*registerCallback)(App::SteamCallbackBase*, int) = nullptr;
} s_api;

template<typename T>
void Resolve(T& aOut, const char* acName)
{
    aOut = reinterpret_cast<T>(GetProcAddress(s_api.module, acName));
}

void* FirstInterface(std::initializer_list<const char*> aAccessors)
{
    for (const auto* name : aAccessors)
    {
        if (const auto get = reinterpret_cast<Accessor_t>(GetProcAddress(s_api.module, name)))
        {
            if (auto* iface = get())
                return iface;
        }
    }
    return nullptr;
}

constexpr int kLobbyFriendsOnly = 1;
constexpr int kMaxMembers = 4;
constexpr int kLobbyEnterId = 504;          // LobbyEnter_t
constexpr int kLobbyChatUpdateId = 506;     // LobbyChatUpdate_t
constexpr int kLobbyJoinRequestedId = 333;  // GameLobbyJoinRequested_t
constexpr uint32_t kEnterSuccess = 1;
constexpr uint32_t kMemberEntered = 1;

#pragma pack(push, 8)
struct LobbyEnter
{
    uint64_t lobby;
    uint32_t chatPermissions;
    bool locked;
    uint32_t response;
};
struct LobbyChatUpdate
{
    uint64_t lobby;
    uint64_t userChanged;
    uint64_t makingChange;
    uint32_t stateChange;
};
struct LobbyJoinRequested
{
    uint64_t lobby;
    uint64_t friendId;
};
#pragma pack(pop)

template<typename TData, void (App::SteamLobby::*AHandler)(const void*)>
struct Callback final : App::SteamCallbackBase
{
    void Run(void* apParam) override { (App::SteamLobby::Get().*AHandler)(apParam); }
    void Run(void* apParam, bool, uint64_t) override { Run(apParam); }
    int GetCallbackSizeBytes() override { return sizeof(TData); }
};

Callback<LobbyEnter, &App::SteamLobby::OnLobbyEnter> s_enterCallback;
Callback<LobbyChatUpdate, &App::SteamLobby::OnLobbyChatUpdate> s_chatCallback;
Callback<LobbyJoinRequested, &App::SteamLobby::OnLobbyJoinRequested> s_joinCallback;

std::string Str(const char* acValue)
{
    return acValue ? std::string(acValue) : std::string();
}

std::string ReadVersion()
{
    std::ifstream in(GCyberpunkMpLocation / "version.txt");
    std::string version;
    std::getline(in, version);
    return version.empty() ? std::string("dev") : version;
}

// "+connect_lobby <id>": Steam adds this when it starts the game from a lobby invite.
uint64_t LobbyFromCommandLine()
{
    const std::wstring cmd = GetCommandLineW();
    const auto pos = cmd.find(L"+connect_lobby");
    if (pos == std::wstring::npos)
        return 0;
    return std::wcstoull(cmd.c_str() + pos + 14, nullptr, 10);
}
} // namespace

namespace App
{
SteamLobby& SteamLobby::Get()
{
    static SteamLobby s_instance;
    return s_instance;
}

bool SteamLobby::Init()
{
    if (m_available)
        return true;

    s_api.module = GetModuleHandleW(L"steam_api64.dll");
    if (!s_api.module)
        return false;

    s_api.mm = FirstInterface({"SteamAPI_SteamMatchmaking_v009"});
    s_api.friends = FirstInterface({"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017"});
    s_api.utils = FirstInterface({"SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009"});
    auto* user = FirstInterface({"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021",
                                 "SteamAPI_SteamUser_v020"});

    Resolve(s_api.createLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
    Resolve(s_api.joinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
    Resolve(s_api.leaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    Resolve(s_api.numMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    Resolve(s_api.memberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    Resolve(s_api.getData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
    Resolve(s_api.setData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    Resolve(s_api.getMemberData, "SteamAPI_ISteamMatchmaking_GetLobbyMemberData");
    Resolve(s_api.setMemberData, "SteamAPI_ISteamMatchmaking_SetLobbyMemberData");
    Resolve(s_api.owner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    Resolve(s_api.personaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    Resolve(s_api.mediumAvatar, "SteamAPI_ISteamFriends_GetMediumFriendAvatar");
    Resolve(s_api.setRichPresence, "SteamAPI_ISteamFriends_SetRichPresence");
    Resolve(s_api.imageSize, "SteamAPI_ISteamUtils_GetImageSize");
    Resolve(s_api.imageRgba, "SteamAPI_ISteamUtils_GetImageRGBA");
    Resolve(s_api.registerCallback, "SteamAPI_RegisterCallback");
    uint64_t (*getSteamId)(ISteamUser*) = nullptr;
    Resolve(getSteamId, "SteamAPI_ISteamUser_GetSteamID");

    const bool ok = s_api.mm && s_api.friends && s_api.createLobby && s_api.joinLobby && s_api.numMembers &&
                    s_api.memberByIndex && s_api.getData && s_api.setData && s_api.getMemberData &&
                    s_api.setMemberData && s_api.owner && s_api.registerCallback && user && getSteamId;
    if (!ok)
    {
        if (!m_initTried)
            spdlog::warn("[Party] Steam lobbies are not available in this steam_api64.dll: no party panel");
        m_initTried = true;
        return false;
    }

    m_localSteamId = getSteamId(user);
    if (!m_localSteamId)
        return false;

    m_version = ReadVersion();

    s_enterCallback.callbackId = kLobbyEnterId;
    s_api.registerCallback(&s_enterCallback, kLobbyEnterId);
    s_chatCallback.callbackId = kLobbyChatUpdateId;
    s_api.registerCallback(&s_chatCallback, kLobbyChatUpdateId);
    s_joinCallback.callbackId = kLobbyJoinRequestedId;
    s_api.registerCallback(&s_joinCallback, kLobbyJoinRequestedId);

    m_available = true;
    m_initTried = true;
    spdlog::info("[Party] Steam lobbies ready (version {})", m_version);

    if (const auto lobby = LobbyFromCommandLine())
    {
        spdlog::info("[Party] started from a lobby invite ({})", lobby);
        Join(lobby);
    }
    return true;
}

void SteamLobby::Host(const std::string& acConnect)
{
    if (!Init())
        return;

    m_pendingConnect = acConnect;
    if (m_lobby)
    {
        s_api.setData(s_api.mm, m_lobby, "connect", acConnect.c_str());
        return;
    }
    if (m_creating)
        return;

    m_creating = true;
    s_api.createLobby(s_api.mm, kLobbyFriendsOnly, kMaxMembers);
    spdlog::info("[Party] creating the party lobby");
}

void SteamLobby::Join(uint64_t aLobbyId)
{
    if (!Init() || !aLobbyId || aLobbyId == m_lobby)
        return;

    if (m_lobby)
        Leave();

    s_api.joinLobby(s_api.mm, aLobbyId);
    spdlog::info("[Party] joining lobby {}", aLobbyId);
}

void SteamLobby::Leave()
{
    if (!m_available || !m_lobby)
        return;

    if (s_api.leaveLobby)
        s_api.leaveLobby(s_api.mm, m_lobby);
    if (s_api.setRichPresence)
    {
        s_api.setRichPresence(s_api.friends, "steam_player_group", nullptr);
        s_api.setRichPresence(s_api.friends, "steam_player_group_size", nullptr);
    }
    m_lobby = 0;

    std::lock_guard _(m_lock);
    m_snapshot = {};
}

void SteamLobby::SetLocalState(const char* acState)
{
    m_localState = acState ? acState : "";
    PublishLocal();
}

void SteamLobby::SetLocalConnected(bool aConnected)
{
    m_localConnected = aConnected;
    PublishLocal();
}

void SteamLobby::PublishLocal()
{
    if (!m_available || !m_lobby)
        return;

    s_api.setMemberData(s_api.mm, m_lobby, "state", m_localState.c_str());
    s_api.setMemberData(s_api.mm, m_lobby, "connected", m_localConnected ? "1" : "0");
    s_api.setMemberData(s_api.mm, m_lobby, "version", m_version.c_str());
    m_nextRefresh = {};
}

void SteamLobby::OnLobbyEnter(const void* apData)
{
    const auto* data = static_cast<const LobbyEnter*>(apData);
    m_creating = false;

    if (data->response != kEnterSuccess)
    {
        spdlog::warn("[Party] could not enter lobby {} (response {})", data->lobby, data->response);
        Overlay::Toast("Could not join the party");
        return;
    }

    m_lobby = data->lobby;
    const bool isHost = s_api.owner(s_api.mm, m_lobby) == m_localSteamId;
    spdlog::info("[Party] entered lobby {} as {}", m_lobby, isHost ? "host" : "member");

    if (s_api.setRichPresence)
    {
        const auto group = std::to_string(m_lobby);
        s_api.setRichPresence(s_api.friends, "steam_player_group", group.c_str());
        s_api.setRichPresence(s_api.friends, "steam_player_group_size", "1");
    }

    if (isHost)
    {
        s_api.setData(s_api.mm, m_lobby, "version", m_version.c_str());
        s_api.setData(s_api.mm, m_lobby, "connect", m_pendingConnect.c_str());
        s_api.setData(s_api.mm, m_lobby, "game", "ns-cybermp");
        Overlay::Toast("Party created: invite friends from the Steam overlay");
    }
    else
    {
        const auto hostVersion = Str(s_api.getData(s_api.mm, m_lobby, "version"));
        const auto connect = Str(s_api.getData(s_api.mm, m_lobby, "connect"));

        if (hostVersion != m_version)
        {
            spdlog::warn("[Party] version mismatch: host {} vs {}", hostVersion, m_version);
            Overlay::Toast(fmt::format("Version mismatch: host has {}, you have {}. Update before joining.", hostVersion,
                                       m_version));
        }
        else if (Settings::ApplyConnectString(connect))
        {
            Settings::Get().enabled = true;
            Settings::Get().autoConnect = true;

            // Already in the world: connect now; otherwise when a save is loaded.
            auto* system = Red::GetGameSystem<NetworkWorldSystem>();
            if (system && system->IsPlayerReady() && !Core::Container::Get<NetworkService>()->IsConnected())
                system->Connect();
            else
                Overlay::Toast("Joined the party: load a save once the host is in game");
        }
        else
        {
            spdlog::warn("[Party] lobby has no connect info: '{}'", connect);
        }
    }

    PublishLocal();
}

void SteamLobby::OnLobbyChatUpdate(const void* apData)
{
    const auto* data = static_cast<const LobbyChatUpdate*>(apData);
    if (data->lobby != m_lobby || data->userChanged == m_localSteamId)
        return;

    const auto name = s_api.personaName ? Str(s_api.personaName(s_api.friends, data->userChanged)) : std::string();
    const bool entered = (data->stateChange & kMemberEntered) != 0;
    Overlay::Toast(fmt::format("{} {} the party", name.empty() ? "A player" : name, entered ? "joined" : "left"));
    m_nextRefresh = {};
}

void SteamLobby::OnLobbyJoinRequested(const void* apData)
{
    const auto* data = static_cast<const LobbyJoinRequested*>(apData);
    spdlog::info("[Party] invite accepted: lobby {} from {}", data->lobby, data->friendId);
    Join(data->lobby);
}

void SteamLobby::Update()
{
    if (!m_available)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextRefresh)
        return;
    m_nextRefresh = now + std::chrono::milliseconds(500);

    Refresh();
}

void SteamLobby::Refresh()
{
    Snapshot snapshot;

    if (m_lobby)
    {
        snapshot.inLobby = true;
        const uint64_t ownerId = s_api.owner(s_api.mm, m_lobby);
        snapshot.isHost = ownerId == m_localSteamId;
        const auto hostVersion = Str(s_api.getData(s_api.mm, m_lobby, "version"));
        snapshot.versionMismatch = !snapshot.isHost && hostVersion != m_version;

        const int count = s_api.numMembers(s_api.mm, m_lobby);
        for (int i = 0; i < count && i < kMaxMembers; ++i)
        {
            Member member;
            member.steamId = s_api.memberByIndex(s_api.mm, m_lobby, i);
            member.isHost = member.steamId == ownerId;
            member.isLocal = member.steamId == m_localSteamId;
            member.name = s_api.personaName ? Str(s_api.personaName(s_api.friends, member.steamId)) : std::string();
            member.state = Str(s_api.getMemberData(s_api.mm, m_lobby, member.steamId, "state"));
            member.connected = Str(s_api.getMemberData(s_api.mm, m_lobby, member.steamId, "connected")) == "1";
            member.version = Str(s_api.getMemberData(s_api.mm, m_lobby, member.steamId, "version"));
            member.avatarHandle = s_api.mediumAvatar ? s_api.mediumAvatar(s_api.friends, member.steamId) : 0;
            if (member.isHost)
                snapshot.hostInGame = member.state == "ingame";
            snapshot.members.push_back(std::move(member));
        }

        // Host first, then the others in lobby order.
        std::stable_sort(snapshot.members.begin(), snapshot.members.end(),
                         [](const Member& a, const Member& b) { return a.isHost && !b.isHost; });

        if (snapshot.versionMismatch)
            snapshot.status = fmt::format("Version mismatch: host {}, you {}", hostVersion, m_version);
        else if (snapshot.isHost)
            snapshot.status = m_localConnected ? "Hosting: you are in game" : "Hosting: load your save to start";
        else if (m_localConnected)
            snapshot.status = "Connected to the party";
        else if (snapshot.hostInGame)
            snapshot.status = "Host is in game: load your save";
        else
            snapshot.status = "Waiting for the host to load in";

        if (s_api.setRichPresence)
        {
            const auto size = std::to_string(snapshot.members.size());
            s_api.setRichPresence(s_api.friends, "steam_player_group_size", size.c_str());
        }
    }
    else
    {
        // Not in a party: still show yourself, so the panel confirms the mod and Steam are working.
        Member self;
        self.steamId = m_localSteamId;
        self.isLocal = true;
        self.name = s_api.personaName ? Str(s_api.personaName(s_api.friends, m_localSteamId)) : std::string();
        self.avatarHandle = s_api.mediumAvatar ? s_api.mediumAvatar(s_api.friends, m_localSteamId) : 0;
        self.state = m_localState;
        snapshot.members.push_back(std::move(self));
        snapshot.status = Settings::Get().host ? "Starting the party..." : "Not in a party";
    }

    std::lock_guard _(m_lock);
    m_snapshot = std::move(snapshot);
}

SteamLobby::Snapshot SteamLobby::GetSnapshot()
{
    std::lock_guard _(m_lock);
    return m_snapshot;
}

bool SteamLobby::GetAvatar(int aHandle, std::vector<uint8_t>& aRgba, uint32_t& aWidth, uint32_t& aHeight)
{
    if (!m_available || aHandle <= 0 || !s_api.utils || !s_api.imageSize || !s_api.imageRgba)
        return false;
    if (!s_api.imageSize(s_api.utils, aHandle, &aWidth, &aHeight) || aWidth == 0 || aHeight == 0 || aWidth > 512 ||
        aHeight > 512)
        return false;

    aRgba.resize(static_cast<size_t>(aWidth) * aHeight * 4);
    return s_api.imageRgba(s_api.utils, aHandle, aRgba.data(), static_cast<int>(aRgba.size()));
}
} // namespace App
