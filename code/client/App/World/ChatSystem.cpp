#include "ChatSystem.h"

#include "App/Network/NetworkService.h"
#include "App/ChatMessageEvent.h"
#include "NetworkWorldSystem.h"
#include <RED4ext/Scripting/Natives/Generated/game/ui/IGameSystemUI.hpp>
#include <RED4ext/Scripting/Natives/Generated/red/Event.hpp>

void ChatSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    m_ready = true;
}

void ChatSystem::OnAfterWorldDetach()
{
    m_ready = false;
}

void ChatSystem::HandleChatMessage(const PacketEvent<server::ChatMessage>& aMessage)
{
    m_messages.push_back(aMessage);

    // "/mark ..." from anyone: a line both players' logs share, to line up the two logs at a moment of interest.
    const std::string text = aMessage.get_message().c_str();
    if (text.rfind("/mark", 0) == 0)
        spdlog::warn("[MARK] ===== from {}: {} (tick {}) =====", aMessage.get_username().c_str(), text.substr(5),
                     NetworkWorldSystem::GetTick());
    else
        spdlog::info("[Chat] {}: {}", aMessage.get_username().c_str(), text);

    auto evt = reinterpret_cast<ChatMessageUIEvent*>(Red::GetClass("ChatMessageUIEvent")->CreateInstance());
    evt->author = RED4ext::CString(aMessage.get_username());
    evt->message = RED4ext::CString(aMessage.get_message());

    // QueueEvent through the script-visible UISystem function (the fork declared it as a native virtual).
    auto uiSystem = Red::GetGameSystem<RED4ext::game::ui::IGameSystemUI>();
    Red::Handle<Red::red::Event> handle(reinterpret_cast<Red::red::Event*>(evt));
    if (!Red::CallVirtual(uiSystem, "QueueEvent", handle))
        spdlog::warn("[Chat] UISystem.QueueEvent failed");
}

void ChatSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&ChatSystem::HandleChatMessage>(this);
}

void ChatSystem::Send(const Red::CString& aMessage)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    const std::string text = aMessage.c_str();
    if (text.rfind("/mark", 0) == 0)
        spdlog::warn("[MARK] ===== me: {} (tick {}) =====", text.substr(5), NetworkWorldSystem::GetTick());
    if (!pNetworkService->IsConnected())
        return;

    client::ChatMessageRequest request;
    request.set_message(aMessage.c_str());

    pNetworkService->Send(request);
}

RED4ext::CString ChatSystem::GetUsername()
{
    return RED4ext::CString("jackhumbert");
}
