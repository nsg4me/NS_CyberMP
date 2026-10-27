#include "NetworkService.h"
#include "App/Steam/SteamService.h"
#include "App/Steam/SteamLobby.h"
#include "App/Steam/SteamRelay.h"
#include "App/World/SyncDiagnostics.h"
#include "App/Rendering/Overlay.h"

#include "App/World/NetworkWorldSystem.h"
#include "Game/Utils.h"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/Object.hpp"
#include "App/World/AppearanceSystem.h"
#include "Game/CharacterCustomizationSystem.h"

NetworkService::NetworkService()
    : Client(client::kIdentifier, server::kIdentifier)
    , m_lastUpdate(std::chrono::steady_clock::now())
{
    BindMessageHandlers();
}

NetworkService::~NetworkService()
{
}

void NetworkService::BindMessageHandlers()
{
    GetSink<server::AuthenticationResponse>().connect<&NetworkService::HandleAuthentication>(this);
}

void NetworkService::OnConsume(const void* apData, uint32_t aSize)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    if(!server::Deserializer::Process(reader, 0, m_dispatcher))
    {
        spdlog::error("Failed to deserialize a message from the server.");
    }
}

void NetworkService::OnConnected()
{
    spdlog::info("[Net] connected to the server, authenticating");

    client::AuthenticationRequest request;
    // Steam name when available (the server shows it in chat and the player list); the server has no auth.
    std::string username = Core::Container::Get<App::SteamService>()->GetPersonaName();
    if (username.empty())
        username = "Player";

    request.set_token("");
    request.set_username(username.c_str());
    request.set_steam_id(App::SteamRelay::Get().GetLocalSteamId());
    request.set_client_protocol(client::kIdentifier);
    request.set_server_protocol(server::kIdentifier);

    spdlog::info("[Net] auth request: name '{}', SteamID {}, protocol client {:x} / server {:x}", username,
                 App::SteamRelay::Get().GetLocalSteamId(), client::kIdentifier, server::kIdentifier);
    Send(request);
}

void NetworkService::OnDisconnected(EDisconnectReason aReason)
{
    static constexpr const char* kReasons[] = {"timeout (never connected)", "local problem (connection lost)",
                                               "closed by the server", "address not resolved", "aborted by us",
                                               "normal"};
    const auto index = static_cast<uint32_t>(aReason);
    spdlog::info("[Net] disconnected: {} ({})", index < std::size(kReasons) ? kReasons[index] : "?", index);
    Red::GetGameSystem<NetworkWorldSystem>()->OnDisconnected(aReason);

    m_authenticated = false;
    App::SteamLobby::Get().SetLocalConnected(false);

    switch (aReason)
    {
    case kNormal:
    case kAborted: // left on purpose (disconnect key, quitting)
        App::Overlay::Toast("Disconnected from the party");
        m_reconnectAt.reset();
        m_reconnectAttempts = 0;
        break;
    case kKicked:
        App::Overlay::Toast("You were removed from the session by the host");
        m_reconnectAt.reset();
        break;
    default:
        if (m_reconnectAttempts < 5)
        {
            ++m_reconnectAttempts;
            m_reconnectAt = std::chrono::steady_clock::now() + std::chrono::seconds(3 * m_reconnectAttempts);
            App::Overlay::Toast(fmt::format("Connection lost: reconnecting (attempt {} of 5)...", m_reconnectAttempts));
        }
        else
        {
            m_reconnectAt.reset();
            App::Overlay::Toast("Could not reconnect. Hold the connect key (default /) to try again.");
        }
        break;
    }
}

void NetworkService::OnUpdate()
{
    m_dispatcher.update();

    Red::GetGameSystem<NetworkWorldSystem>()->Update(GetClock().GetCurrentTick());
}

void NetworkService::OnGameUpdate(RED4ext::CGameApplication* apApp)
{
    Update();

    // Network health every 5 s while connected: latency, quality, throughput and what is still queued.
    if (IsConnected() && SyncDiagnostics::Every("net-status", std::chrono::seconds(5)))
    {
        const auto status = GetConnectionStatus();
        const auto stats = GetStatistics();
        spdlog::info("[Net] ping {} ms, quality local {:.0f}% / remote {:.0f}%, out {:.1f} KB/s, in {:.1f} KB/s, "
                     "queued reliable {} B / unreliable {} B, server tick {}",
                     status.m_nPing, status.m_flConnectionQualityLocal * 100.f,
                     status.m_flConnectionQualityRemote * 100.f, stats.SentBytes / 1024.f, stats.RecvBytes / 1024.f,
                     status.m_cbPendingReliable, status.m_cbPendingUnreliable, GetClock().GetCurrentTick());
    }

    if (m_reconnectAt && std::chrono::steady_clock::now() >= *m_reconnectAt)
    {
        m_reconnectAt.reset();
        auto* system = Red::GetGameSystem<NetworkWorldSystem>();
        if (system && system->IsPlayerReady() && !IsConnected())
            system->Connect();
    }
}

void NetworkService::HandleAuthentication(const PacketEvent<server::AuthenticationResponse>& aResponse)
{
    spdlog::info("[Net] auth response: {}{}", aResponse.get_success() ? "accepted" : "REFUSED: ",
                 aResponse.get_success() ? "" : aResponse.get_error().c_str());
    if (!aResponse.get_success())
    {
        // Handle the error message here
        // aResponse.get_error();
        Close();
        return;
    }

    m_settings = aResponse.get_settings();
    spdlog::info("[Net] server update rate {} Hz", m_settings.get_update_rate());

    m_reconnectAttempts = 0;
    m_reconnectAt.reset();
    App::SteamLobby::Get().SetLocalConnected(true);
    App::Overlay::Toast("Connected to the party");

    Red::GetGameSystem<NetworkWorldSystem>()->OnConnected();

    RequestSpawn();
}

void NetworkService::RequestSpawn()
{
    client::SpawnCharacterRequest request;
    request.set_is_player(true);

    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;
    system->GetLocalPlayerControlledGameObject(player);
    if (!player || !player->transformComponent)
    {
        spdlog::warn("[Spawn] no local player to spawn yet");
        return;
    }

    const auto cEntityPosition = player->transformComponent->worldTransform.Position.AsVector3();
    const auto cEntityRotation = Game::ToGlm(player->transformComponent->worldTransform.Orientation);

    common::Vector3 pos;
    pos.set_x(cEntityPosition.X);
    pos.set_y(cEntityPosition.Y);
    pos.set_z(cEntityPosition.Z);

    request.set_position(pos);
    request.set_rotation(cEntityRotation.z);
    request.set_cookie(0);

    auto appSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    request.set_equipment(appSystem->GetPlayerItems(player));


    auto ccSystem = Red::GetGameSystem<Red::game::ui::CharacterCustomizationSystem>();
    auto stateHandle = GetCustomizationState(ccSystem);
    if (stateHandle) 
    {
        auto writer = CMPWriter();
        CharacterCustomizationState_Serialize(stateHandle->instance, &writer);
        spdlog::info("[Appearance] sending my customization: {} bytes", writer.bytes.size());
        request.set_ccstate(writer.bytes);
    } 
    else
    {
        spdlog::warn("[Appearance] my customization state is not available: friends will see a default look");
    }

    spdlog::info("[Spawn] asking the server to spawn my character at ({:.1f}, {:.1f}, {:.1f})", cEntityPosition.X,
                 cEntityPosition.Y, cEntityPosition.Z);
    Send(request);
}

ScratchAllocator& NetworkService::GetScratch()
{
    thread_local ScratchAllocator s_allocator{1 << 19};
    return s_allocator;
}
