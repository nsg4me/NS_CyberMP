#pragma once

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
} // namespace SyncDiagnostics
