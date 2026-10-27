#include "MinHookProvider.hpp"

#include <MinHook.h>

Support::MinHookProvider::MinHookProvider()
{
    MH_Initialize();

    SetDefault(*this);
}

Support::MinHookProvider::~MinHookProvider()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

bool Support::MinHookProvider::HookAttach(uintptr_t aAddress, void* aCallback)
{
    return HookAttach(aAddress, aCallback, nullptr);
}

bool Support::MinHookProvider::HookAttach(uintptr_t aAddress, void* aCallback, void** aOriginal)
{
    // Logged with the address relative to the game executable, so a failing hook can be matched to the game build.
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto where = aAddress >= base ? aAddress - base : aAddress;

    if (!aAddress)
    {
        spdlog::error("[Hook] target address not found (address library lookup failed)");
        return false;
    }

    if (const auto status = MH_CreateHook(reinterpret_cast<void*>(aAddress), aCallback, aOriginal); status != MH_OK)
    {
        spdlog::error("[Hook] exe+{:#x}: create failed ({})", where, MH_StatusToString(status));
        return false;
    }

    if (const auto status = MH_EnableHook(reinterpret_cast<void*>(aAddress)); status != MH_OK)
    {
        spdlog::error("[Hook] exe+{:#x}: enable failed ({})", where, MH_StatusToString(status));
        MH_RemoveHook(reinterpret_cast<void*>(aAddress));
        return false;
    }

    spdlog::debug("[Hook] exe+{:#x} attached", where);
    return true;
}

bool Support::MinHookProvider::HookDetach(uintptr_t aAddress)
{
    if (MH_DisableHook(reinterpret_cast<void*>(aAddress)) != MH_OK)
        return false;

    if (MH_RemoveHook(reinterpret_cast<void*>(aAddress)) != MH_OK)
        return false;

    return true;
}

void Support::MinHookProvider::Enqueue(std::function<void()> aFunc)
{
    m_queue.Add(std::move(aFunc));
}

void Support::MinHookProvider::OnGameUpdate(RED4ext::CGameApplication* apApp)
{
    m_queue.Drain();
}
