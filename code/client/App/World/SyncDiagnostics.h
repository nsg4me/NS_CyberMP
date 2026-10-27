#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// Counters for the movement/vehicle sync, summarised in CyberpunkMP.log every few seconds while connected, so a
// test session shows where sync stops: sending, receiving, controller attach, or applying the position.
namespace SyncDiagnostics
{
inline std::atomic<uint32_t> MovesSent{0};
inline std::atomic<uint32_t> VehicleMovesSent{0};
inline std::atomic<uint32_t> MovesReceived{0};
inline std::atomic<uint32_t> ControllerHookCalls{0};
inline std::atomic<uint32_t> ControllersAttached{0};
inline std::atomic<uint32_t> CharacterApplies{0};
inline std::atomic<uint32_t> VehicleApplies{0};
inline std::atomic<uint32_t> NoControllerFrames{0};
inline std::atomic<uint32_t> NoStubFrames{0};
inline std::atomic<uint32_t> MovesUnknownEntity{0}; // moves for a server id we have not spawned
inline std::atomic<uint32_t> MovesOutOfOrder{0};    // older than the newest one already queued
inline std::atomic<uint32_t> StarvedFrames{0};      // interpolation ran out of queued positions
inline std::atomic<uint32_t> ControllerOrphans{0};  // controller attached to a puppet we no longer track
inline std::atomic<uint64_t> LastReceivedTick{0};   // tick stamped on the newest move received

// True at most once per interval for a key: for logging things that happen every frame without flooding the log.
inline bool Every(std::string_view aKey, std::chrono::milliseconds aInterval)
{
    static std::mutex s_lock;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_next;

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard _(s_lock);
    auto& next = s_next[std::string(aKey)];
    if (now < next)
        return false;
    next = now + aInterval;
    return true;
}
} // namespace SyncDiagnostics
