#include "GameServer.h"
#include "ServerDiagnostics.h"

#include <Components/PlayerComponent.h>
#include <Components/VehicleComponent.h>

#include "Core/Filesystem.h"
#include "Game/Level.h"
#include "Scripting/IRpc.h"
#include "Scripting/RpcScriptInstance.h"

#include "PlayerManager.h"


using nlohmann::json;

GameServer* GServer = nullptr;

GameServer::GameServer()
    : Server(client::kIdentifier, server::kIdentifier)
    , m_log(m_path)
    , m_lastUpdate(std::chrono::steady_clock::now())
{
    GServer = this;

    try
    {
        auto serverPath = GetPath();

        std::ifstream config(serverPath / "config" / "server.json");
        if (!config.is_open())
        {
            spdlog::info("No configuration file found, creating config/server.json");
            std::error_code ec;
            create_directory(serverPath / "config", ec);

            std::ofstream of(serverPath / "config" / "server.json");
            json data = m_config;
            of << std::setw(4) << data;
        }
        else
        {
            json data = json::parse(config);
            m_config = data.get<Config>();
        }
    }
    catch (std::exception& e)
    {
        spdlog::error("Error parsing config.json: {}", e.what());
        m_run = false;
        return;
    }

    uint16_t port = m_config.Port;
    while (!Host(port, m_config.TickRate))
    {
        spdlog::warn("Port {} is already in use, trying {}", port, port + 1);
        port++;
    }

    m_pWorld = MakeUnique<World>(m_config.GetFlecsConfig());
    m_pWorld->GetScriptInstance()->Initialize();

    RegisterHandler<&GameServer::HandleAuthentication>(this);

    spdlog::info("Server started on port {}", GetPort());
}

GameServer::~GameServer()
{
    GServer = nullptr;
}

void GameServer::Kill()
{
    m_run = false;
    Close();
}

void GameServer::Run()
{
    while (m_run && IsListening())
        Update();
}

void GameServer::OnUpdate()
{
    const auto now = std::chrono::steady_clock::now();
    const auto delta = now - m_lastUpdate;
    m_lastUpdate = now;

    m_tasks.Drain();
    m_dispatcher.update();

    m_pWorld->Update(std::chrono::duration_cast<std::chrono::duration<float>>(delta).count());

    ServerDiagnostics::Report();
}

void GameServer::OnConsume(const void* apData, uint32_t aSize, ConnectionId aConnectionId)
{
    // ReSharper disable once CppCStyleCast
    ViewBuffer buffer((uint8_t*)apData, aSize);  // NOLINT(clang-diagnostic-cast-qual)
    Buffer::Reader reader(&buffer);

    if (const auto result = client::Deserializer::Process(reader, aConnectionId, m_dispatcher); !result)
    {
        spdlog::error("Failed to deserialize packet from {}", aConnectionId);
    }
}

void GameServer::OnConnection(ConnectionId aHandle)
{
    spdlog::info("[Net] connection {:x} opened", aHandle);
}

void GameServer::OnDisconnection(ConnectionId aConnectionId, EDisconnectReason aReason)
{
    spdlog::info("[Net] connection {:x} closed (reason {})", aConnectionId, static_cast<int>(aReason));

    auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();

    if (const auto player = pPlayerManager->GetByConnectionId(aConnectionId))
    {
        pPlayerManager->Remove(player);

        if (auto* pPlayerComponent = player.get<PlayerComponent>())
        {
            GetWorld()->get_mut<Level>()->Remove(pPlayerComponent->Puppet);
        }

        // Their shared vehicles go with them (destroying the player would delete them silently, leaving every other
        // player with a parked ghost car).
        std::vector<flecs::entity> vehicles;
        player.children([&vehicles](flecs::entity aChild) {
            if (aChild.has<VehicleComponent>())
                vehicles.push_back(aChild);
        });
        for (auto vehicle : vehicles)
        {
            spdlog::info("[Vehicle] removing vehicle {:x} of the player who left", vehicle.id());
            GetWorld()->get_mut<Level>()->Remove(vehicle);
        }

        if (auto* pPlayerComponent = player.get<PlayerComponent>())
            spdlog::info("[Net] player '{}' (steam {}) left", pPlayerComponent->Username.c_str(), pPlayerComponent->SteamId);
        player.destruct();

        spdlog::info("{}/{} player(s) online", pPlayerManager->Count(), m_config.MaxPlayer);
    }
}

void GameServer::HandleAuthentication(const PacketEvent<client::AuthenticationRequest>& aRequest)
{
    server::AuthenticationResponse response;

    if (aRequest.get_client_protocol() != client::kIdentifier)
    {
        response.set_success(false);
        response.set_error("Invalid protocol version!");

        spdlog::warn("Connection attempt with client identifier {:x}, expected {:x}", aRequest.get_client_protocol(), client::kIdentifier);
        Send(aRequest.ConnectionId, response);
        Kick(aRequest.ConnectionId);
        return;
    }

    if (aRequest.get_server_protocol() != server::kIdentifier)
    {
        response.set_success(false);
        response.set_error("Invalid protocol version!");

        spdlog::warn("Connection attempt with server identifier {:x}, expected {:x}", aRequest.get_client_protocol(), server::kIdentifier);
        Send(aRequest.ConnectionId, response);
        Kick(aRequest.ConnectionId);
        return;
    }

    spdlog::info("[Net] connection {:x}: player {} (steam {}) authenticated with token {}", aRequest.ConnectionId, aRequest.get_username(), aRequest.get_steam_id(), aRequest.get_token());
    response.set_success(true);

    server::Settings settings;
    settings.set_update_rate(m_config.UpdateRate);
    response.set_settings(settings);

    server::RpcDefinitions definitions;

    const auto* pRpc = static_cast<RpcScriptInstance*>(IRpc::Get());
    pRpc->Serialize(definitions);

    if (!Send(aRequest.ConnectionId, definitions))
        spdlog::error("Failed to send message to {:x}", aRequest.ConnectionId);

    if (!Send(aRequest.ConnectionId, response))
        spdlog::error("Failed to send message to {:x}", aRequest.ConnectionId);

    // The player was accepted, rpc definitions are ready, we can create the player's handle
    auto player = GetWorld()->get_mut<PlayerManager>()->Create(aRequest.ConnectionId, aRequest.get_username());
    if (player)
    {
        if (auto* pPlayer = player.get_mut<PlayerComponent>())
            pPlayer->SteamId = aRequest.get_steam_id();
    }
}

ScratchAllocator& GameServer::GetScratch()
{
    thread_local ScratchAllocator s_allocator{1 << 19};
    return s_allocator;
}
