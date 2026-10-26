#pragma once

#include "Core/Foundation/Feature.hpp"

namespace App
{
// Steam friends integration through the game's own steam_api64.dll (Steam copies only; does nothing elsewhere).
//
// Host:   sets the "connect" rich presence to the server's public address, so friends get "Join Game" on the host
//         in their friends list, and "Invite to Game" works from the Steam overlay.
// Friend: a join request (accepting an invite, or "Join Game") points the client at the host's address and
//         connects. If the game was not running, Steam starts it with the same connect string on the command line.
struct SteamService : Core::Feature
{
    SteamService();
    ~SteamService() override;

    void OnGameUpdate(RED4ext::CGameApplication* apApp) override;
    void OnShutdown() override;

    // Opens the Steam overlay's invite dialog for the current session. Returns false when Steam is unavailable.
    bool OpenInviteDialog();

    // The player's Steam name (empty when Steam is unavailable); used as the in-game/chat name.
    std::string GetPersonaName() const;

    // Called from the Steam callback (Steam's thread or the game's callback pump).
    void OnJoinRequested(uint64_t aFriendSteamId, const char* acConnect);

private:
    bool Attach();
    void UpdatePresence();

    bool m_attachTried = false;
    bool m_attached = false;
    bool m_presenceSet = false;
    uint64_t m_presenceLobby = 0;
    std::chrono::steady_clock::time_point m_nextAttempt{};
    int m_attempts = 0;
};
} // namespace App
