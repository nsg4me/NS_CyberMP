
#include "VehicleSystem.h"

#include "App/Network/NetworkService.h"
#include "App/Steam/SteamLobby.h"
#include "RED4ext/Scripting/Natives/Generated/game/Puppet.hpp"
#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include "RED4ext/Scripting/Natives/Generated/vehicle/WheeledBaseObject.hpp"

#include "NetworkWorldSystem.h"
#include "Game/Utils.h"
#include "Game/Compat.h"
#include "App/Components/EntityComponent.h"
#include "App/Components/AttachedComponent.h"
#include "App/Components/SpawningComponent.h"
#include "App/Components/InterpolationComponent.h"


void VehicleSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    m_ready = true;
    Red::CallVirtual(this, "OnWorldAttached");
}

void VehicleSystem::OnAfterWorldDetach()
{
    m_ready = false;
    // The world (and every vehicle in it) is gone
    m_vehicleRemoteId = std::nullopt;
    m_vehicleGameId = std::nullopt;
    m_pendingMounts.clear();
}

void VehicleSystem::OnDisconnected()
{
    m_vehicleRemoteId = std::nullopt;
    m_vehicleGameId = std::nullopt;
}

void VehicleSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleLoadMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleEnterMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleExitMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleControlMessage>(this);

    m_pSpawnVehicle = Red::Detail::GetFunction(GetType(), "SpawnVehicle");
    m_pEnterVehicle = Red::Detail::GetFunction(GetType(), "EnterVehicle");
    m_pExitVehicle = Red::Detail::GetFunction(GetType(), "ExitVehicle");
}

std::optional<uint64_t> VehicleSystem::GetVehicleRemoteId() const
{
    return m_vehicleRemoteId;
}

std::optional<Red::EntityID> VehicleSystem::GetVehicleGameId() const
{
    return m_vehicleGameId;
}

void VehicleSystem::OnVehicleEnter(Red::EntityID aVehicle, const Red::TweakDBID& aVehicleTdbid, Red::CName aName, const Red::Vector4& aPostion, const Red::Quaternion& aOrientation)
{
    spdlog::info("[Vehicle] I entered vehicle game id {} ({:016x}) seat {} at ({:.1f}, {:.1f}, {:.1f})", aVehicle.hash,
                 aVehicleTdbid.value, aName.ToString(), aPostion.X, aPostion.Y, aPostion.Z);
    App::SteamLobby::Get().SetActivity("vehicle", true);
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    if (!pNetworkService->IsConnected())
        return;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    if (!handle->GetRemotePlayerId())
    {
        spdlog::warn("[Vehicle] not telling the server: our character has no server id yet");
        return;
    }

    client::EnterVehicleRequest request;
    request.set_id(*handle->GetRemotePlayerId());
    request.set_vehicle_id(aVehicleTdbid.value);
    request.set_sit_id(aName.hash);

    const auto serverVehicle = handle->FindEntity(aVehicle);
    if (serverVehicle)
    {
        request.set_remote_vehicle_id(serverVehicle);
        spdlog::info("[Vehicle]   it is another player's vehicle (server id {}): we are a passenger, they drive it",
                     serverVehicle.id());

        m_vehicleGameId = std::nullopt;
    }
    else
    {
        const auto cEntityRotation = eulerAngles(Game::ToGlm(aOrientation));

        common::Vector3 position;
        position.set_x(aPostion.X);
        position.set_y(aPostion.Y);
        position.set_z(aPostion.Z);
        request.set_position(position);
        request.set_rotation(cEntityRotation.z);
        spdlog::info("[Vehicle]   it is our own vehicle: asking the server to share it (we send its position while "
                     "the server says we control it)");

        m_vehicleGameId = aVehicle;
    }

    pNetworkService->Send(request);
}

void VehicleSystem::OnVehicleExit()
{
    spdlog::info("[Vehicle] I left the vehicle (was game id {}, server id {})",
                 m_vehicleGameId ? std::to_string(m_vehicleGameId->hash) : std::string("-"),
                 m_vehicleRemoteId ? std::to_string(*m_vehicleRemoteId) : std::string("-"));
    m_vehicleGameId = std::nullopt;
    m_vehicleRemoteId = std::nullopt;
    App::SteamLobby::Get().SetActivity("vehicle", false);

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    if (!pNetworkService->IsConnected())
        return;

    client::ExitVehicleRequest request;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    if (!handle->GetRemotePlayerId())
        return;
    request.set_id(*handle->GetRemotePlayerId());

    pNetworkService->Send(request);
}

bool VehicleSystem::HandleVehicleLoadMessage(const PacketEvent<server::NotifyVehicleLoad>& aMessage)
{
    spdlog::info("[Vehicle] server says load vehicle {} ({:016x}) at ({:.1f}, {:.1f}, {:.1f})", aMessage.get_id(),
                 aMessage.get_tweak_id(), aMessage.get_position().get_x(), aMessage.get_position().get_y(),
                 aMessage.get_position().get_z());
    const auto handle = Red::Handle(this);
    Red::EntityID id;
    Red::ScriptGameInstance game;

    Red::Vector4 position;
    position.X = aMessage.get_position().get_x();
    position.Y = aMessage.get_position().get_y();
    position.Z = aMessage.get_position().get_z();

    const auto eulerAngles = glm::vec3(0.f, 0.f, aMessage.get_rotation());
    const auto quat = glm::quat(eulerAngles);
    Red::Quaternion rotation = Game::ToRed(quat);

    if (!Red::Detail::CallFunctionWithArgs(m_pSpawnVehicle, handle, id, aMessage.get_tweak_id(), position, rotation))
    {
        spdlog::error("[Vehicle]   SpawnVehicle (script) failed for {}", aMessage.get_id());
        return false;
    }

    if (!id.IsDynamic())
    {
        spdlog::error("[Vehicle]   vehicle {} got a non-dynamic game id {}", aMessage.get_id(), id.hash);
        return false;
    }
    spdlog::info("[Vehicle]   spawning as game id {}", id.hash);

    // spdlog::info("[VehicleSystem] * Spawned: {}, {}", aMessage.get_id(), id.hash);
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    worldSystem->make_alive(aMessage.get_id()).emplace<SpawningComponent>(id);

    return true;
}

bool VehicleSystem::HandleVehicleEnterMessage(const PacketEvent<server::NotifyVehicleEnter>& aMessage)
{
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();

    const auto sit = Red::CName(aMessage.get_sit_id());
    const auto character = worldSystem->GetEntityByServerId(aMessage.get_character_id());
    spdlog::info("[Vehicle] server says character {} entered vehicle {} seat {} (our vehicle: server id {}, game id {})",
                 aMessage.get_character_id(), aMessage.get_vehicle_id(), sit.ToString(),
                 m_vehicleRemoteId ? std::to_string(*m_vehicleRemoteId) : std::string("-"),
                 m_vehicleGameId ? std::to_string(m_vehicleGameId->hash) : std::string("-"));

    if (!character)
    {
        spdlog::warn("[Vehicle]   character {} is not spawned here; ignored", aMessage.get_character_id());
        return true;
    }

    if (m_vehicleRemoteId && aMessage.get_vehicle_id() == *m_vehicleRemoteId)
    {
        if (!m_vehicleGameId)
        {
            spdlog::warn("[Vehicle]   it is the vehicle we control but we have no game id for it; ignored");
            return true;
        }
        spdlog::info("[Vehicle]   it is the vehicle we control: seating them in it");
        DoMount(character, *m_vehicleGameId, sit);
    }
    else
    {
        const auto entity = worldSystem->GetEntityByServerId(aMessage.get_vehicle_id());
        if (entity && entity.has<EntityComponent>())
        {
            spdlog::info("[Vehicle]   vehicle is in the world: seating them now");
            DoMount(character, entity.get<EntityComponent>()->Id, sit);
        }
        else
        {
            const auto vehicle = worldSystem->GetEntityIdByServerId(aMessage.get_vehicle_id());
            spdlog::info("[Vehicle]   vehicle not ready yet (game id {}): seating them when it spawns", vehicle.hash);
            if (vehicle.hash == 0)
                spdlog::warn("[Vehicle]   vehicle {} is not known here at all: this mount will never happen",
                             aMessage.get_vehicle_id());
            m_pendingMounts[vehicle].push_back(aMessage);
        }
    }

    // 
    // const auto sit = Red::CName(aMessage.get_sit_id());

    // if (!Red::Detail::CallFunctionWithArgs(m_pEnterVehicle, handle, res, character, vehicle, sit))
    //     return false;

    return true;
}

void VehicleSystem::OnVehicleReady(const Red::EntityID& aVehicleEntityId)
{
    spdlog::info("[Vehicle] vehicle game id {} is ready ({} pending mounts)", aVehicleEntityId.hash,
                 m_pendingMounts.count(aVehicleEntityId) ? m_pendingMounts[aVehicleEntityId].size() : 0);

    if (m_pendingMounts.find(aVehicleEntityId) != m_pendingMounts.end())
    {
        const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();

        for (auto& message : m_pendingMounts[aVehicleEntityId])
        {
            const auto sit = Red::CName(message.get_sit_id());
            const auto character = worldSystem->GetEntityByServerId(message.get_character_id());

            auto vehicleEntity = worldSystem->GetEntityByServerId(message.get_vehicle_id());
            vehicleEntity.emplace<EntityComponent>(aVehicleEntityId, true, nullptr);
            vehicleEntity.remove<SpawningComponent>();

            const auto vehicle = worldSystem->GetEntityIdByServerId(message.get_vehicle_id());

            DoMount(character, vehicle, sit);
        }

        m_pendingMounts.erase(aVehicleEntityId);
    }
    else
    {
        spdlog::info("[Vehicle]   nobody was waiting for it");
    }
}

bool VehicleSystem::HandleVehicleExitMessage(const PacketEvent<server::NotifyVehicleExit>& aMessage)
{
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto handle = Red::Handle(this);
    bool res = false;
    const auto character = worldSystem->GetEntityIdByServerId(aMessage.get_character_id());
    spdlog::info("[Vehicle] server says character {} (game id {}) left their vehicle", aMessage.get_character_id(),
                 character.hash);
    if (character.hash == 0)
        return true;
    Red::Detail::CallFunctionWithArgs(m_pExitVehicle, handle, res, character);
    spdlog::info("[Vehicle]   ExitVehicle -> {}", res);

    auto characterEntity = worldSystem->GetEntityByServerId(aMessage.get_character_id());

    characterEntity.remove<AttachedComponent>();

    return true;
}

bool VehicleSystem::HandleVehicleControlMessage(const PacketEvent<server::NotifyVehicleControlAssigned>& aMessage)
{
    spdlog::info("[Vehicle] server says we now control vehicle {} (was {}), our game id for it {}",
                 aMessage.get_vehicle_id(), m_vehicleRemoteId ? std::to_string(*m_vehicleRemoteId) : std::string("-"),
                 m_vehicleGameId ? std::to_string(m_vehicleGameId->hash) : std::string("none: positions will NOT be sent"));
    m_vehicleRemoteId = aMessage.get_vehicle_id();
    return true;
}

void VehicleSystem::DoMount(flecs::entity aCharacter, Red::EntityID aVehicle, Red::CName aSit)
{
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto character = worldSystem->GetEntityIdByServerId(aCharacter);
    const auto vehicle = Red::Cast<Red::vehicle::WheeledBaseObject>(worldSystem->GetEntity(aVehicle));
    const auto handle = Red::Handle(this);
    bool res = false;

    if (!vehicle)
    {
        spdlog::warn("[Vehicle] mount: game id {} is not a wheeled vehicle (or not loaded); character {} not seated",
                     aVehicle.hash, aCharacter.id());
        return;
    }

    Red::Detail::CallFunctionWithArgs(m_pEnterVehicle, handle, res, character, vehicle->entityID, aSit);
    spdlog::info("[Vehicle] mount: character {} (game id {}) into vehicle {} seat {} -> {}; {}", aCharacter.id(),
                 character.hash, aVehicle.hash, aSit.ToString(), res,
                 (!m_vehicleGameId || *m_vehicleGameId != aVehicle) ? "remote-driven (kinematic, follows the network)"
                                                                     : "we drive it");

    aCharacter.add<AttachedComponent>();

    if (!m_vehicleGameId || *m_vehicleGameId != aVehicle)
    {
        // called from vehicle::actions::DriveAction::OnStart
        // static Core::RawFunc<4018412273UL, void (*)(Red::move::Component *, IMoveController &)> AttachLocomotionController
        static Core::RawFunc<4039776020UL, void (*)(Red::vehicle::BaseObject*, bool)> SetIsPlayerControlled;
        static Core::RawFunc<1620777158UL, void (*)(Red::vehicle::BaseObject*, uint32_t)> SetFlags;
        static Core::RawFunc<1585713002UL, void (*)(Red::vehicle::BaseObject*, bool)> SetKinematic;

        // AttachLocomotionController(component, controller);
        SetIsPlayerControlled(vehicle, false);
        // turn on engine
        Game::Compat::TurnEngineOn(vehicle, true);
        Game::Compat::ClearEngineFlag61(vehicle);
        SetFlags(vehicle, 0x10);
        SetFlags(vehicle, 0x80);
        SetKinematic(vehicle, true);
    }
}

