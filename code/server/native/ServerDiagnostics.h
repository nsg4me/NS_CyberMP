#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

// Movement/vehicle traffic per entity, summarised in the server log every 10 s, so a desync can be traced to a client
// that stopped sending, moves the server rejected, or replication that never went out. Server thread only.
namespace ServerDiagnostics
{
struct EntityStats
{
    uint64_t moves = 0;      // since the last report
    uint64_t total = 0;
    uint64_t rejected = 0;   // since the last report
    uint64_t backwards = 0;  // older tick than the previous move, since the last report
    glm::vec3 last{};
    uint64_t lastTick = 0;
    uint64_t connection = 0;
    bool vehicle = false;
    bool silentLogged = false;
    std::chrono::steady_clock::time_point lastMove{};
};

inline std::unordered_map<uint64_t, EntityStats> Entities;
inline uint64_t Replicated = 0; // NotifyEntityMove messages sent, since the last report
inline std::chrono::steady_clock::time_point NextReport{};

inline bool Every(const std::string& aKey, std::chrono::milliseconds aInterval)
{
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_next;
    const auto now = std::chrono::steady_clock::now();
    auto& next = s_next[aKey];
    if (now < next)
        return false;
    next = now + aInterval;
    return true;
}

inline void Report()
{
    const auto now = std::chrono::steady_clock::now();
    if (now < NextReport)
        return;
    NextReport = now + std::chrono::seconds(10);
    if (Entities.empty())
        return;

    spdlog::info("[Sync] 10s: {} move messages sent to players", Replicated);
    Replicated = 0;
    for (auto& [id, s] : Entities)
    {
        const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.lastMove).count();
        if (s.moves == 0 && s.rejected == 0)
        {
            if (!s.silentLogged)
            {
                s.silentLogged = true;
                spdlog::warn("[Sync]   {} {:x} (connection {:x}): no moves for {} ms", s.vehicle ? "vehicle" : "character",
                             id, s.connection, idle);
            }
            continue;
        }
        s.silentLogged = false;
        spdlog::info("[Sync]   {} {:x} (connection {:x}): {} moves ({} total), {} rejected, {} out of order, last at "
                     "({:.1f}, {:.1f}, {:.1f}) tick {}",
                     s.vehicle ? "vehicle" : "character", id, s.connection, s.moves, s.total, s.rejected, s.backwards,
                     s.last.x, s.last.y, s.last.z, s.lastTick);
        s.moves = s.rejected = s.backwards = 0;
    }
}
} // namespace ServerDiagnostics
