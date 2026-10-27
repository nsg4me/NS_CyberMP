#include "NetworkWorldSystem.h"

#include <App/Settings.h>

#include "App/Network/NetworkService.h"
#include "App/Steam/SteamRelay.h"
#include "App/Steam/SteamLobby.h"
#include "App/Rendering/Overlay.h"
#include "App/Rendering/PartyColours.h"
#include "SyncDiagnostics.h"
#include "RED4ext/Scripting/Natives/Generated/game/Puppet.hpp"
#include "RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp"
#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingFacility.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingInfo.hpp>
#include <RED4ext/Scripting/Natives/Generated/vehicle/MoveSystem.hpp>

#include "App/Components/EntityComponent.h"
#include "App/Components/SpawningComponent.h"
#include "App/Components/InterpolationComponent.h"
#include "Game/Utils.h"
#include "Game/Compat.h"
#include "Game/CharacterCustomizationSystem.h"

#include "ChatSystem.h"

static uint64_t GTick = 0;

uint64_t NetworkWorldSystem::GetTick()
{
    return GTick;
}

NetworkWorldSystem::NetworkWorldSystem()
{
    set_entity_range(10'000'000, 20'000'000);
}

bool NetworkWorldSystem::Spawn(uint64_t aServerId, const Red::Vector4& aPosition, const Red::Quaternion& aRotation, const Red::DynArray<Red::TweakDBID>& aEquipment, const Vector<uint8_t> aCcstate)
{
    if (!m_ready)
    {
        spdlog::warn("[Spawn] server id {}: world not ready, not spawned", aServerId);
        return false;
    }

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    Red::EntityID id;
    Red::ScriptGameInstance game;

    Red::Handle<game::ui::CharacterCustomizationState> stateHandle;
    CreateHandle_CharacterCustomizationState(&stateHandle);

    auto reader = CMPReader(aCcstate);
    CharacterCustomizationState_Serialize(stateHandle.instance, &reader);
    const bool male = Game::Compat::IsBodyGenderMale(stateHandle.instance);

    if (!Red::Detail::CallFunctionWithArgs(m_pCreatePuppet, handle, id, aPosition, aRotation, male))
    {
        spdlog::error("[Spawn] server id {}: CreatePuppet (script) failed", aServerId);
        return false;
    }
    spdlog::info("[Spawn] server id {}: CreatePuppet -> game id {} (dynamic {}), body {}, {} equipment items, "
                 "customization {} bytes", aServerId, id.hash, id.IsDynamic(), male ? "male" : "female",
                 aEquipment.Size(), aCcstate.size());

    auto apprSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    apprSystem->AddEntity(id, aEquipment, aCcstate);

    if (!id.IsDynamic())
    {
        spdlog::error("[Spawn] server id {}: game id {} is not a dynamic entity, not tracked", aServerId, id.hash);
        return false;
    }

    make_alive(aServerId).emplace<SpawningComponent>(id);
    m_spawnStarted[aServerId] = std::chrono::steady_clock::now();

    return true;
}

void NetworkWorldSystem::DeSpawn(uint64_t aServerId) const
{
    const auto entity = GetEntityByServerId(aServerId);

    if (!entity)
    {
        spdlog::info("[Spawn] despawn server id {}: not tracked here", aServerId);
        return;
    }

    if (auto* pEntity = entity.get<EntityComponent>())
        spdlog::info("[Spawn] despawn server id {} (game id {}, {})", aServerId, pEntity->Id.hash,
                     pEntity->IsVehicle ? "vehicle" : "character");
    else if (auto* pSpawning = entity.get<SpawningComponent>())
        spdlog::info("[Spawn] despawn server id {} (game id {}, was still spawning)", aServerId, pSpawning->Id.hash);

    if (auto* pEntity = entity.get<EntityComponent>())
    {
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }
    else if (auto* pEntity = entity.get<SpawningComponent>())
    {
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }

    entity.destruct();
}

Red::Handle<Red::Entity> NetworkWorldSystem::GetEntity(Red::EntityID aId) const
{
    Red::Handle<Red::IGameSystem> dynamicEntitySystem;
    if (!Red::CallStatic("ScriptGameInstance", "GetDynamicEntitySystem", dynamicEntitySystem))
        return nullptr;

    Red::Handle<Red::Entity> entity;
    if (!Red::CallVirtual(dynamicEntitySystem, "GetEntity", entity, aId))
        return nullptr;

    return entity;
}

flecs::entity NetworkWorldSystem::GetEntityByServerId(uint64_t aServerId) const
{
    return flecs::entity(*this, aServerId);
}

Red::EntityID NetworkWorldSystem::GetEntityIdByServerId(uint64_t aServerId) const
{
    const auto entity = GetEntityByServerId(aServerId);
    if (!entity)
        return 0;

    if (auto* pEntity = entity.get<EntityComponent>())
        return pEntity->Id;

    if (auto* pEntity = entity.get<SpawningComponent>())
        return pEntity->Id;

    return 0;
}

flecs::entity NetworkWorldSystem::FindEntity(Red::EntityID aId) const
{
    auto entity = query<EntityComponent>().find(
        [aId](const EntityComponent& component)
        {
            return component.Id == aId;
        });

    if (!entity)
    {
        entity = query<SpawningComponent>().find(
            [aId](const SpawningComponent& component)
            {
                return component.Id == aId;
            });
    }

    return entity;
}

void NetworkWorldSystem::Update(uint64_t aTick)
{
    GTick = aTick;

    // Long frames (loading, stutters) are when positions jump; logged so they can be matched with desyncs.
    const auto frameNow = std::chrono::steady_clock::now();
    if (m_lastFrame.time_since_epoch().count() != 0)
    {
        const auto frameMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameNow - m_lastFrame).count();
        if (frameMs > 250 && SyncDiagnostics::Every("hitch", std::chrono::seconds(2)))
            spdlog::warn("[Perf] frame took {} ms", frameMs);
    }
    m_lastFrame = frameNow;

    const auto delta = std::min(aTick - m_lastTick, 1000ull);
    m_lastTick = aTick;

    const auto service = Core::Container::Get<NetworkService>();
    if (service && service->IsConnected())
    {
        progress(static_cast<float>(delta) / 1000.f);
        ReportSync();
    }
}

// Every 5 s while connected: what the sync did, and where each remote character is versus where it should be.
void NetworkWorldSystem::ReportSync()
{
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextSyncReport)
        return;
    m_nextSyncReport = now + std::chrono::seconds(5);

    using namespace SyncDiagnostics;
    spdlog::info("[Sync] 5s: sent {} moves ({} vehicle), received {} (unknown id {}, out of order {}), controller hook "
                 "{} / attached {} / orphaned {}, applied {} char + {} vehicle, frames without controller {}, without "
                 "stub {}, starved {}",
                 MovesSent.exchange(0), VehicleMovesSent.exchange(0), MovesReceived.exchange(0),
                 MovesUnknownEntity.exchange(0), MovesOutOfOrder.exchange(0), ControllerHookCalls.exchange(0),
                 ControllersAttached.load(), ControllerOrphans.load(), CharacterApplies.exchange(0),
                 VehicleApplies.exchange(0), NoControllerFrames.exchange(0), NoStubFrames.exchange(0),
                 StarvedFrames.exchange(0));

    // Interpolation plays positions back ~100 ms behind the clock; if the clocks disagree a lot, characters freeze.
    if (const auto last = LastReceivedTick.load())
        spdlog::info("[Sync]   clock: local tick {}, newest received move tick {} (difference {} ms)", GTick, last,
                     static_cast<int64_t>(GTick) - static_cast<int64_t>(last));

    if (const auto local = GetLocalPlayerPosition())
        spdlog::info("[Sync]   me: server id {}, at ({:.1f}, {:.1f}, {:.1f}){}",
                     GetRemotePlayerId() ? std::to_string(*GetRemotePlayerId()) : std::string("none yet"), local->X,
                     local->Y, local->Z, GetVehicleSystem() && GetVehicleSystem()->GetVehicleGameId() ? ", in a vehicle" : "");

    each(
        [this](flecs::entity aEntity, const SpawningComponent& aSpawning)
        {
            spdlog::info("[Sync]   server id {} still spawning (game id {}, controller {})", aEntity.id(),
                         aSpawning.Id.hash, aSpawning.Controller ? "yes" : "no");
        });

    each(
        [this](flecs::entity aEntity, const EntityComponent& aComponent)
        {
            const auto* pInterpolation = aEntity.get<InterpolationComponent>();
            std::string target = "none";
            float error = -1.f;
            if (pInterpolation && !pInterpolation->TimePoints.empty())
            {
                const auto& last = pInterpolation->TimePoints.back().Position;
                target = fmt::format("({:.1f}, {:.1f}, {:.1f})", last.x, last.y, last.z);
                if (const auto actual = GetRemotePlayerPosition(aEntity.id()))
                {
                    const float dx = actual->X - last.x, dy = actual->Y - last.y, dz = actual->Z - last.z;
                    error = std::sqrt(dx * dx + dy * dy + dz * dz);
                }
            }
            long long age = -1;
            if (pInterpolation && pInterpolation->Received)
                age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                            pInterpolation->LastReceived).count();
            std::string who;
            if (const auto it = m_remotePlayers.find(aEntity.id()); it != m_remotePlayers.end())
                who = fmt::format(" ({})", it->second.name);
            float distance = -1.f;
            if (const auto actual = GetRemotePlayerPosition(aEntity.id()))
                if (const auto local = GetLocalPlayerPosition())
                {
                    const float dx = actual->X - local->X, dy = actual->Y - local->Y, dz = actual->Z - local->Z;
                    distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                }
            spdlog::info("[Sync]   {} {}{}: game id {}, controller {}, moves received {}, last {} ms ago, queued {}, "
                         "latest target {}, drawn off by {:.1f} m, {:.0f} m from me",
                         aComponent.IsVehicle ? "vehicle" : "character", aEntity.id(), who, aComponent.Id.hash,
                         aComponent.Controller ? "yes" : "NO", pInterpolation ? pInterpolation->Received : 0, age,
                         pInterpolation ? pInterpolation->TimePoints.size() : 0, target, error, distance);
        });
}

void NetworkWorldSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnWorldAttached");
    IGameSystem::OnWorldAttached(aScene);

    m_chatSystem->OnWorldAttached(aScene);
    m_appearanceSystem->OnWorldAttached(aScene);
    m_interpolationSystem->OnWorldAttached(aScene);
    m_vehicleSystem->OnWorldAttached(aScene);

    m_ready = true;
}

void NetworkWorldSystem::OnAfterWorldDetach()
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnAfterWorldDetach");
    m_ready = false;
    m_playerReady = false;
    App::Overlay::SetMainMenuVisible(false);

    m_interpolationSystem->OnAfterWorldDetach();
    m_chatSystem->OnAfterWorldDetach();
    m_vehicleSystem->OnAfterWorldDetach();

    m_remotePlayerId = std::nullopt;

    IGameSystem::OnAfterWorldDetach();
}

void NetworkWorldSystem::OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }
    IGameSystem::OnBeforeWorldDetach(aScene);

    m_appearanceSystem->OnBeforeWorldDetach(aScene);
}

void NetworkWorldSystem::HandleCharacterLoad(const PacketEvent<server::NotifyCharacterLoad>& aMessage)
{
    auto& pos = aMessage.get_position();
    auto& rot = aMessage.get_rotation();

    const Red::Vector4 position{pos.get_x(), pos.get_y(), pos.get_z(), 1.f};
    const auto eulerAngles = glm::vec3(0.f, 0.f, rot);
    const auto quat = glm::quat(eulerAngles);

    const Red::Quaternion rotation{quat.x, quat.y, quat.z, quat.w};

    auto equipment = Red::DynArray<Red::TweakDBID>(this->GetAllocator());
    for (auto item : aMessage.get_equipment())
    {
        equipment.EmplaceBack(item);
    }

    auto ccstate = aMessage.get_ccstate();

    spdlog::info("[Spawn] server says load {} {} '{}' (steam {}) at ({:.1f}, {:.1f}, {:.1f}) facing {:.2f}, {} equipment, "
                 "customization {} bytes", aMessage.get_is_player() ? "player" : "character", aMessage.get_id(),
                 aMessage.get_username().c_str(), aMessage.get_steam_id(), pos.get_x(), pos.get_y(), pos.get_z(), rot,
                 equipment.Size(), ccstate.size());
    if (GetRemotePlayerId() && *GetRemotePlayerId() == aMessage.get_id())
        spdlog::warn("[Spawn] server id {} is our own character: the server sent us ourselves", aMessage.get_id());
    for (const auto& item : equipment)
        spdlog::debug("[Spawn]   equipment {:016x}", item.value);

    if (aMessage.get_is_player() && (aMessage.get_steam_id() || !aMessage.get_username().empty()))
    {
        m_remotePlayers[aMessage.get_id()] =
            RemotePlayer{aMessage.get_id(), std::string(aMessage.get_username().c_str()), aMessage.get_steam_id()};
    }

    Spawn(aMessage.get_id(), position, rotation, equipment, ccstate);
}

std::vector<NetworkWorldSystem::RemotePlayer> NetworkWorldSystem::GetRemotePlayers() const
{
    std::vector<RemotePlayer> players;
    players.reserve(m_remotePlayers.size());
    for (const auto& [id, player] : m_remotePlayers)
        players.push_back(player);
    return players;
}

std::optional<Red::Vector4> NetworkWorldSystem::GetRemotePlayerPosition(uint64_t aServerId) const
{
    const auto entityId = GetEntityIdByServerId(aServerId);
    if (entityId.hash == 0)
        return std::nullopt;

    const auto entity = GetEntity(entityId);
    auto* object = Red::Cast<Red::GameObject>(entity.GetPtr());
    if (!object || !object->transformComponent)
        return std::nullopt;

    const auto& position = object->transformComponent->localTransform.Position;
    const auto v = position.AsVector3();
    return Red::Vector4{v.X, v.Y, v.Z, 1.f};
}

std::optional<Red::Vector4> NetworkWorldSystem::GetLocalPlayerPosition() const
{
    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    if (!system)
        return std::nullopt;

    Red::Handle<Red::GameObject> player;
    system->GetLocalPlayerControlledGameObject(player);
    if (!player || !player->transformComponent)
        return std::nullopt;

    const auto v = player->transformComponent->localTransform.Position.AsVector3();
    return Red::Vector4{v.X, v.Y, v.Z, 1.f};
}

void NetworkWorldSystem::HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage)
{
    spdlog::info("[Spawn] server says unload {}", aMessage.get_id());
    m_spawnStarted.erase(aMessage.get_id());
    m_remotePlayers.erase(aMessage.get_id());
    DeSpawn(aMessage.get_id());
}

void NetworkWorldSystem::HandleSpawnCharacterResponse(const PacketEvent<server::SpawnCharacterResponse>& aMessage)
{
    if (!aMessage.has_id())
    {
        spdlog::error("[Spawn] the server refused to spawn our character");
        return;
    }

    spdlog::info("[Spawn] server spawned our character as server id {}", aMessage.get_id());
    SetRemotePlayerId(aMessage.get_id());
}

// Local movement: what we send, and jumps that look like teleports (fast travel, respawn, scene placement).
static void NoteLocalSend(const char* acKind, float aX, float aY, float aZ, float aSpeed, uint64_t aServerId)
{
    static bool s_have = false;
    static float s_x = 0, s_y = 0, s_z = 0;
    if (s_have)
    {
        const float dx = aX - s_x, dy = aY - s_y, dz = aZ - s_z;
        const float jump = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (jump > 30.f)
            spdlog::warn("[Move] my {} jumped {:.0f} m to ({:.1f}, {:.1f}, {:.1f}) (teleport / fast travel / respawn?)",
                         acKind, jump, aX, aY, aZ);
    }
    s_have = true;
    s_x = aX;
    s_y = aY;
    s_z = aZ;

    if (SyncDiagnostics::Every("local-send", std::chrono::seconds(5)))
        spdlog::info("[Move] sending my {} (server id {}): ({:.1f}, {:.1f}, {:.1f}) speed {:.1f}, tick {}", acKind,
                     aServerId, aX, aY, aZ, aSpeed, NetworkWorldSystem::GetTick());
}

static Core::RawFunc<
    1160782872UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, bool)>
    IsMountedToObject;
static Core::RawFunc<
    3120376212UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, Red::game::mounting::MountingInfo &)>
    GetMountingInfo;

void NetworkWorldSystem::UpdatePlayerLocation() const
{
    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;
    system->GetLocalPlayerControlledGameObject(player);

    if (!player || !GetRemotePlayerId())
        return;

    auto puppet = Red::Cast<Red::game::Puppet>(player);

    // const auto mountingFacility = Red::GetGameSystem<Red::game::mounting::MountingFacility>();
    // Red::game::mounting::MountingInfo mountingInfo;
    // if (GetMountingInfo(mountingFacility, *puppet.instance, Red::CName("seat_front_left"), mountingInfo)) {
    if (auto vehicle_id = GetVehicleSystem()->GetVehicleGameId())
    {
        // auto vehicle = Red::Cast<Red::vehicle::BaseObject>(GetEntity(mountingInfo.parentId));
        auto vehicle = Red::Cast<Red::vehicle::BaseObject>(GetEntity(*vehicle_id));
        if (!vehicle)
        {
            if (SyncDiagnostics::Every("vehicle-missing", std::chrono::seconds(5)))
                spdlog::warn("[Move] my vehicle game id {} is not loaded: sending nothing", (*vehicle_id).hash);
            return;
        }
        if (auto remote_id = GetVehicleSystem()->GetVehicleRemoteId(); remote_id != std::nullopt)
        {
            auto transform = Red::WorldTransform();

            transform = Game::Compat::GetWorldTransform(vehicle);

            // const auto cEntityRotation = eulerAngles(Game::ToGlm(vehicle->placedComponent->worldTransform.Orientation));

            // about the same
            // transform = vehicle->worldTransform;

            // about the same
            // transform = vehicle->runtimeData->transform;

            // try to get transform from move system
            // seems to return 0 :/
            // auto transform = Red::WorldTransform();
            // const auto moveSystem = Red::GetGameSystem<Red::vehicle::MoveSystem>();
            // // GetCurrentTransform
            // reinterpret_cast<void (*)(const Red::vehicle::MoveSystem *, const Red::EntityID &, Red::WorldTransform*)>(*(uintptr_t*)(*(uintptr_t*)moveSystem + 0x250))(moveSystem, vehicle->id, &transform);

            const auto cEntityPosition = transform.Position.AsVector3();
            const auto cEntityRotation = eulerAngles(Game::ToGlm(transform.Orientation));
            float speed = Game::Compat::GetSpeed(vehicle);

            common::Vector3 pos;
            pos.set_x(cEntityPosition.X);
            pos.set_y(cEntityPosition.Y);
            pos.set_z(cEntityPosition.Z);

            common::Vector3 rot;
            rot.set_x(cEntityRotation.x);
            rot.set_y(cEntityRotation.y);
            rot.set_z(cEntityRotation.z);

            client::MoveEntityRequest request;
            request.set_position(pos);
            request.set_full_rotation(rot);
            request.set_id(*remote_id);
            request.set_speed(speed);
            request.set_tick(GetTick());

            const auto pNetworkService = Core::Container::Get<NetworkService>();
            pNetworkService->Send(request);
            ++SyncDiagnostics::VehicleMovesSent;

            client::MoveEntityRequest characterRequest;
            characterRequest.set_position(pos);
            characterRequest.set_rotation(cEntityRotation.z);
            characterRequest.set_id(*GetRemotePlayerId());
            characterRequest.set_speed(speed);
            characterRequest.set_tick(GetTick());

            pNetworkService->Send(characterRequest);
            NoteLocalSend("vehicle", cEntityPosition.X, cEntityPosition.Y, cEntityPosition.Z, speed, *remote_id);
            if (SyncDiagnostics::Every("vehicle-physics", std::chrono::seconds(5)))
                spdlog::info("[Move]   my vehicle game id {}: physics state 0x{:x}", vehicle->entityID.hash,
                             Game::Compat::GetPhysicsState(vehicle));
        }
        else if (SyncDiagnostics::Every("vehicle-no-control", std::chrono::seconds(5)))
        {
            spdlog::info("[Move] in my vehicle (game id {}) but the server has not given us control yet: not sending "
                         "its position", (*vehicle_id).hash);
        }
    }
    else
    {
        const auto cEntityPosition = puppet->transformComponent->localTransform.Position.AsVector3();
        const auto cEntityRotation = eulerAngles(Game::ToGlm(puppet->transformComponent->worldTransform.Orientation));
        const auto* pMoveComponent = Game::Compat::GetMoveComponent(puppet);
        float speed = pMoveComponent ? pMoveComponent->speed.Magnitude() : 0.f;

        common::Vector3 pos;
        pos.set_x(cEntityPosition.X);
        pos.set_y(cEntityPosition.Y);
        pos.set_z(cEntityPosition.Z);

        client::MoveEntityRequest request;
        request.set_position(pos);
        request.set_rotation(cEntityRotation.z);
        request.set_id(*GetRemotePlayerId());
        request.set_speed(speed);
        request.set_tick(GetTick());

        const auto pNetworkService = Core::Container::Get<NetworkService>();
        pNetworkService->Send(request);
        ++SyncDiagnostics::MovesSent;
        NoteLocalSend("character", cEntityPosition.X, cEntityPosition.Y, cEntityPosition.Z, speed, *GetRemotePlayerId());
        if (!pMoveComponent && SyncDiagnostics::Every("no-move-component", std::chrono::seconds(10)))
            spdlog::warn("[Move] my character has no move component: speed sent as 0 (remote players see me sliding)");
    }

    // if (GetEntityByServerId(*GetRemotePlayerId()).get_mut<InterpolationComponent>()->Attached)
    // {
    //     auto vehicle_id = GetVehicleSystem()->GetVehicle(player->id);
    //     if (!vehicle_id)
    //     {
    //         spdlog::warn("No vehicle for player({})", player->id.hash);
    //         return;
    //     }
    //     auto entity = GetEntity(vehicle_id);
    //     if (!entity)
    //     {
    //         spdlog::warn("No entity for vehicle({})", vehicle_id.hash);
    //         return;
    //     }
    //     auto vehicle = Red::Cast<Red::vehicle::BaseObject>(entity);
    //     if (!vehicle)
    //     {
    //         spdlog::warn("Entity is not vehicle");
    //         return;
    //     }
    //     else
    //     {
    //         // if (vehicle->placedComponent) {
    //             // entityPosition = vehicle->placedComponent->localTransform.Position;
    //         // } else {
    //             // entityPosition = vehicle->worldTransform.Position;
    //             entityPosition = vehicle->rigidBody->worldPosition;
    //         // }
    //     }
    // }
}

void NetworkWorldSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    spdlog::info("[OnInitialize]");

    IGameSystem::OnInitialize(aJob);

    if (Settings::IsDisabled())
        return;

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleCharacterLoad>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleEntityUnload>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleSpawnCharacterResponse>(this);

    m_remotePlayerId = std::nullopt;

    m_pCreatePuppet = Red::Detail::GetFunction(GetType(), "CreatePuppet");
    m_pDeletePuppet = Red::Detail::GetFunction(GetType(), "DeletePuppet");

    m_interpolationSystem = RED4ext::MakeHandle<InterpolationSystem>();
    m_interpolationSystem->OnInitialize(aJob);

    m_appearanceSystem = RED4ext::MakeHandle<AppearanceSystem>();
    m_appearanceSystem->OnInitialize(aJob);

    m_chatSystem = RED4ext::MakeHandle<ChatSystem>();
    m_chatSystem->OnInitialize(aJob);

    m_vehicleSystem = RED4ext::MakeHandle<VehicleSystem>();
    m_vehicleSystem->OnInitialize(aJob);
}

void NetworkWorldSystem::Connect()
{
    const auto& settings = Settings::Get();
    auto address = fmt::format("{}:{}", settings.ip, settings.port);

    // Invited through Steam: reach the host over the Steam relay via a local tunnel port.
    if (settings.steamHost)
    {
        if (const auto port = App::SteamRelay::Get().StartClient(settings.steamHost))
            address = fmt::format("127.0.0.1:{}", port);
        else
            spdlog::warn("[Relay] Steam relay unavailable, connecting directly to {}", address);
    }

    spdlog::info("[Net] connecting to {}{}", address,
                 settings.steamHost ? fmt::format(" (Steam relay to host {})", settings.steamHost) : std::string());
    if (!Core::Container::Get<NetworkService>()->Connect(address))
        spdlog::error("[Net] could not start the connection to {}", address);
}

void NetworkWorldSystem::SetMainMenuVisible(bool aVisible)
{
    spdlog::info("[Menu] title screen {}", aVisible ? "shown" : "hidden");
    App::Overlay::SetMainMenuVisible(aVisible);
    if (aVisible)
        App::SteamLobby::Get().SetLocalState("menu");
}

void NetworkWorldSystem::Log(const Red::CString& aCategory, const Red::CString& aMessage)
{
    spdlog::info("[Script/{}] {}", aCategory.c_str(), aMessage.c_str());
}

bool NetworkWorldSystem::IsDebugMode() const
{
    return Settings::Get().debugMode;
}

void NetworkWorldSystem::SetDebugMode(bool aEnabled)
{
    Settings::SetDebugMode(aEnabled);
}

void NetworkWorldSystem::SetPauseMenuVisible(bool aVisible)
{
    spdlog::info("[Menu] pause/map menu {}", aVisible ? "opened" : "closed");
    App::Overlay::SetPauseMenuVisible(aVisible);
}

Red::Vector4 NetworkWorldSystem::GetPlayerColor(Red::EntityID aEntity) const
{
    int slot = -1;
    if (const auto entity = FindEntity(aEntity))
    {
        if (const auto it = m_remotePlayers.find(entity.id()); it != m_remotePlayers.end() && it->second.steamId)
            slot = App::SteamLobby::Get().SlotOf(it->second.steamId);
    }

    const auto c = App::PartyColours::ForSlot(slot);
    return Red::Vector4{c.r, c.g, c.b, 1.f};
}

bool NetworkWorldSystem::ShouldAutoConnect()
{
    m_playerReady = true;
    spdlog::info("[World] player is in the world (HUD ready); auto-connect {}, connected {}",
                 Settings::Get().autoConnect ? "on" : "off",
                 Core::Container::Get<NetworkService>()->IsConnected() ? "yes" : "no");
    App::Overlay::SetMainMenuVisible(false);
    App::SteamLobby::Get().SetLocalState("ingame");
    return Settings::Get().autoConnect && !Core::Container::Get<NetworkService>()->IsConnected();
}

void NetworkWorldSystem::Disconnect()
{
    Core::Container::Get<NetworkService>()->Close();
}

void NetworkWorldSystem::OnConnected()
{
    RED4ext::StackArgs_t args;
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnConnected"), nullptr, args);

    const auto pNetworkService = Core::Container::Get<NetworkService>();

    m_updatePlayerLocation = system("Update player location")
        .kind(flecs::OnUpdate)
        .interval(1.f / pNetworkService->GetServerSettings().get_update_rate())
        .run([this](flecs::iter& it)
        {
            UpdatePlayerLocation();
        });

    m_updateSpawningEntities = system<SpawningComponent>("Spawning entity process")
        .interval(0.2f)
        .write<EntityComponent>()
        .write<SpawningComponent>()
        .each([this](flecs::entity aEntity, SpawningComponent& aSpawning)
        {
            const auto pEntity = GetEntity(aSpawning.Id);
            const auto started = m_spawnStarted.find(aEntity.id());
            const auto waited = started != m_spawnStarted.end()
                ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started->second).count()
                : -1;
            if (const auto pOwner = Red::Cast<Red::GameObject>(pEntity))
            {
                if (pOwner->tags.Contains(Red::CName("CyberpunkMP.Vehicle")))
                    return;

                 aEntity.emplace<EntityComponent>(aSpawning.Id, false, aSpawning.Controller);
                 aEntity.remove<SpawningComponent>();
                 pOwner->tags.Add(Red::CName("CyberpunkMP.Puppet"));
                 spdlog::info("[Spawn] server id {}: in the world after {} ms (game id {}, controller {})", aEntity.id(),
                              waited, aSpawning.Id.hash, aSpawning.Controller ? "already attached" : "not yet");
                 if (started != m_spawnStarted.end())
                     m_spawnStarted.erase(started);
            }
            else if (waited > 10000 && SyncDiagnostics::Every(fmt::format("spawn-slow-{}", aEntity.id()), std::chrono::seconds(10)))
            {
                spdlog::warn("[Spawn] server id {}: still no game entity {} s after CreatePuppet (game id {}, entity {})",
                             aEntity.id(), waited / 1000, aSpawning.Id.hash, pEntity ? "exists, not a GameObject" : "not found");
            }
        });

    m_interpolationSystem->OnConnected();
}

void NetworkWorldSystem::OnDisconnected(Client::EDisconnectReason aReason)
{
    spdlog::info("[Spawn] disconnected (reason {}): removing {} remote players", static_cast<int>(aReason),
                 m_remotePlayers.size());
    m_remotePlayers.clear();
    m_spawnStarted.clear();

    each([this](flecs::entity entity, EntityComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    each([this](flecs::entity entity, SpawningComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    if (m_updatePlayerLocation)
        m_updatePlayerLocation.destruct();

    if (m_updateSpawningEntities)
        m_updateSpawningEntities.destruct();

    m_interpolationSystem->OnDisconnected();
    m_vehicleSystem->OnDisconnected();

    RED4ext::StackArgs_t args;
    auto reason = (uint32_t)aReason;
    args.emplace_back(RED4ext::CRTTISystem::Get()->GetType("Uint32"), &reason);
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnDisconnected"), nullptr, args);
}
