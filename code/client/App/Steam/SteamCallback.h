#pragma once

// Same declaration as Steamworks' CCallbackBase (steam_api_common.h). Compiled by MSVC like steam_api64.dll itself,
// so the vtable (including MSVC's reversed order of the two Run overloads) matches what Steam expects.
// Register with SteamAPI_RegisterCallback(callback, id); Steam calls Run from SteamAPI_RunCallbacks (game thread).
namespace App
{
struct SteamCallbackBase
{
    // No virtual destructor: Steam's class has none, and adding one would shift the vtable.
    SteamCallbackBase() = default;
    virtual void Run(void* apParam) = 0;
    virtual void Run(void* apParam, bool aIOFailure, uint64_t aApiCall) = 0;
    virtual int GetCallbackSizeBytes() = 0;

    uint8_t callbackFlags = 0;
    int callbackId = 0;
};
} // namespace App
