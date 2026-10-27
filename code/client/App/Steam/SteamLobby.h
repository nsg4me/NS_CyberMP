#pragma once

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
    };

    struct Snapshot
    {
        bool inLobby = false;
        bool isHost = false;
        std::string status; // one line for the party panel
        bool hostInGame = false;
        bool versionMismatch = false;
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
};
} // namespace App
