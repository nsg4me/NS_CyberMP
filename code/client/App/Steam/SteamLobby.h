#pragma once

#include <unordered_set>

// The "party": a friends-only Steam lobby (up to 4) created by the host.
//
// - Host: creates it when hosting and publishes the connect string and the mod version in the lobby data.
// - Friends: join it from an invite (in game, or "+connect_lobby" when Steam starts the game), check the version, and
//   take the connect string from it; the game then connects once a save is loaded.
// - Everyone publishes a state ("menu", "ingame") and whether they are connected, which the main-menu party boxes and
//   the "host is in game, load your save" hint show.
// - Join/leave events become toasts.
namespace App
{
class SteamLobby
{
public:
    struct Member
    {
        uint64_t steamId = 0;
        std::string name;
        bool isHost = false;
        bool isLocal = false;
        std::string state;   // "menu", "ingame" or empty
        bool connected = false;
        std::string version;
        int avatarHandle = 0; // Steam image handle (0 = none yet)
        std::string activity; // "in game", "paused", "in a vehicle", "in a cutscene", "down", "loading", ...
        int slot = -1;        // party colour slot (0 = host)
    };

    struct Snapshot
    {
        bool inLobby = false;
        bool isHost = false;
        std::string status; // one line for the party panel
        bool hostInGame = false;
        bool versionMismatch = false;
        std::string modMismatch; // empty, or a short summary of how this player's content mods differ from the host's
        std::vector<Member> members;
    };

    static SteamLobby& Get();

    bool Init();
    void Update(); // game thread, every frame: refreshes the snapshot a few times a second

    void Host(const std::string& acConnect);
    void Join(uint64_t aLobbyId);
    void Leave();

    // Host only: remove one player (relay + lobby), or send everyone home.
    void Kick(uint64_t aSteamId);
    void EndSession();

    // Party slot (0 = host) of a SteamID in the current party, or -1. Slot picks the player's colour.
    int SlotOf(uint64_t aSteamId);

    void SetLocalState(const char* acState);
    void SetLocalConnected(bool aConnected);

    // What the local player is doing, shown to the party. Flags: "loading", "menu" (a game menu is open), "scene",
    // "dead", "vehicle".
    void SetActivity(const std::string& acFlag, bool aActive);

    // Share a map position with the party (the waypoint just placed on the world map).
    void SharePing(float aX, float aY, float aZ);

    [[nodiscard]] Snapshot GetSnapshot();
    [[nodiscard]] uint64_t GetLobbyId() const { return m_lobby; }

    // Avatar pixels (RGBA, row-major); false when Steam has not loaded the image yet.
    bool GetAvatar(int aHandle, std::vector<uint8_t>& aRgba, uint32_t& aWidth, uint32_t& aHeight);

    // Steam callbacks
    void OnLobbyEnter(const void* apData);
    void OnLobbyChatUpdate(const void* apData);
    void OnLobbyJoinRequested(const void* apData);

private:
    SteamLobby() = default;

    void Refresh();
    void PublishLocal();
    void LeaveBecause(const char* acToast);
    std::string Activity() const;
    void UpdatePings(const Snapshot& acSnapshot);
    void CheckMods(uint64_t aHostId);

    bool m_available = false;
    bool m_initTried = false;
    uint64_t m_localSteamId = 0;
    uint64_t m_lobby = 0;
    uint64_t m_hostAtJoin = 0;
    bool m_creating = false;
    std::string m_pendingConnect;
    std::string m_localState = "menu";
    bool m_localConnected = false;
    std::string m_version;

    std::mutex m_lock;
    Snapshot m_snapshot;
    std::string m_lastLoggedSummary;
    std::chrono::steady_clock::time_point m_nextRefresh{};

    std::unordered_set<std::string> m_activityFlags;
    std::string m_publishedActivity;

    uint32_t m_pingSeq = 0;
    std::string m_localPing; // "seq;x;y;z"
    struct PingSeen
    {
        std::string value;
        bool initialized = false; // first look at this member done (what was there already is not shown)
        int slot = -1;
        std::chrono::steady_clock::time_point shown{};
        bool visible = false;
    };
    std::unordered_map<uint64_t, PingSeen> m_pings; // other members' pings by SteamID

    std::string m_modsHash;     // ours
    std::string m_checkedHostMods; // the host hash we last compared against
    std::string m_modMismatch;
};
} // namespace App
