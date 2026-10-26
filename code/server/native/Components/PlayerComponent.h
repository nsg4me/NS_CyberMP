#pragma once

struct PlayerComponent
{
    ConnectionId Connection;
    flecs::entity Puppet;
    std::string Username;
    uint64_t SteamId = 0;

    const char* GetUsername() const;

    static void Register(flecs::world& aWorld);
};
