#include "InterpolationSystem.h"
#include "SyncDiagnostics.h"

#include <App/Components/AttachedComponent.h>
#include <App/Threading/ThreadService.h>

#include "App/Network/NetworkService.h"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include "RED4ext/Scripting/Natives/Generated/move/Component.hpp"
#include "RED4ext/Scripting/Natives/Generated/ent/AnimationControllerComponent.hpp"
#include <RED4ext/Scripting/Natives/Generated/vehicle/MoveSystem.hpp>
#include "RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp"
#include "RED4ext/Scripting/Natives/Generated/WorldPosition.hpp"
#include "RED4ext/Scripting/Natives/Vector3.hpp"

#include "Core/Hooking/HookingAgent.hpp"
#include "NetworkWorldSystem.h"
#include "App/Components/EntityComponent.h"
#include "Math/Math.h"
#include "App/Components/InterpolationComponent.h"
#include "App/Components/SpawningComponent.h"
#include "Game/Utils.h"
#include "Game/Compat.h"
#include "Game/Movement.h"
#include "Game/Animation/AnimationData.h"
#include "Game/Animation/MultiMovementController.h"

#include <Math/Spline.h>

inline void ResetWithNewTransform(Red::vehicle::BaseObject* self, const Red::WorldTransform& transform)
{
    reinterpret_cast<void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&)>(*(uintptr_t*)(*(uintptr_t*)self + 0x280))(self, transform);
}

inline void SetSimpleMovement(Red::vehicle::IMoveSystem* apMoveSystem, const Red::EntityID& aEntityId, bool enabled)
{
    reinterpret_cast<void (*)(Red::vehicle::IMoveSystem*, const Red::EntityID&, bool)>(*(uintptr_t*)(*(uintptr_t*)apMoveSystem + 0x1F0))(apMoveSystem, aEntityId, enabled);
}

void InterpolateEntity(flecs::entity aEntity, const EntityComponent& aEntityComponent, InterpolationComponent& aInterpolation, float aSimulationDelay, Red::vehicle::IMoveSystem* apMoveSystem)
{
    // Ticks are milliseconds since 1970 (~1.8e12): far beyond what a float holds exactly (a float steps in ~2 minute
    // increments at that size), which froze interpolation. All tick arithmetic stays in 64-bit integers.
    const int64_t tick = static_cast<int64_t>(NetworkWorldSystem::GetTick()) - static_cast<int64_t>(aSimulationDelay);

    while (!aInterpolation.TimePoints.empty())
    {
        const auto& future = *aInterpolation.TimePoints.begin();
        if (tick > static_cast<int64_t>(future.Tick))
            aInterpolation.TimePoints.pop_front();
        else
            break;
    }

    if (aInterpolation.TimePoints.empty())
    {
        ++SyncDiagnostics::StarvedFrames;
        return;
    }

    // First frame for this entity: start from the first known position, not from the world origin.
    if (aInterpolation.PreviousFrame.Tick == 0)
    {
        aInterpolation.PreviousFrame = aInterpolation.TimePoints.front();
        aInterpolation.PreviousFrame.Tick = static_cast<uint64_t>(tick);
    }

    const auto& first = aInterpolation.PreviousFrame;
    const auto& second = aInterpolation.TimePoints.front();

    // Fraction of the way from the previous drawn frame to the next known position
    auto ratio = 0.0001f;
    const int64_t tickDelta = static_cast<int64_t>(second.Tick) - static_cast<int64_t>(first.Tick);
    if (tickDelta > 0)
    {
        ratio = static_cast<float>(static_cast<double>(tick - static_cast<int64_t>(first.Tick)) /
                                   static_cast<double>(tickDelta));
        ratio = std::clamp(ratio, 0.f, 1.f);
    }

    const auto pEntityStubSystem = Red::GetGameSystem<Red::game::IEntityStubSystem>();
    const auto* pStub = pEntityStubSystem->FindStub(aEntityComponent.Id);
    if (!pStub)
    {
        ++SyncDiagnostics::NoStubFrames;
        return;
    }

    if (aEntity.has<AttachedComponent>())
        return;

    const glm::vec3 position{Lerp(first.Position, second.Position, ratio)};

    if (aEntityComponent.IsVehicle)
    {
        const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
        const auto vehicle = Red::Cast<Red::vehicle::BaseObject>(pSystem->GetEntity(aEntityComponent.Id));
        if (vehicle)
        {
            const auto deltaAngleX{DeltaAngle(first.Rotation.x, second.Rotation.x, true) * ratio};
            const auto directionX = Mod(first.Rotation.x + deltaAngleX, 2.f * static_cast<float>(Pi));

            const auto deltaAngleY{DeltaAngle(first.Rotation.y, second.Rotation.y, true) * ratio};
            const auto directionY = Mod(first.Rotation.y + deltaAngleY, 2.f * static_cast<float>(Pi));

            const auto deltaAngleZ{DeltaAngle(first.Rotation.z, second.Rotation.z, true) * ratio};
            const auto directionZ = Mod(first.Rotation.z + deltaAngleZ, 2.f * static_cast<float>(Pi));

            const auto rot = glm::vec3(directionX, directionY, directionZ);

            auto transform = Red::WorldTransform();
            transform.Position = Red::WorldPosition(Red::Vector4{position.x, position.y, position.z, 0.f});
            transform.Orientation = Game::ToRed(glm::quat(rot));

            static Core::RawFunc<2933986803UL, void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&, float)> ForceMoveTo;
            static Core::RawFunc<457775029UL, void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&, bool)> ForceTransform;
            static Core::RawFunc<2592348558UL, void (*)(Red::vehicle::BaseObject*, uint32_t, bool)> EnablePhysics;

            SetSimpleMovement(apMoveSystem, aEntityComponent.Id, true);

            // modeled after DriveToPoint::OnUpdate_Internal, 3713079867
            if ((Game::Compat::GetPhysicsState(vehicle) & 0x100) == 0)
            {
                EnablePhysics(vehicle, 0x100, false);
            }

            ForceMoveTo(vehicle, transform,
                        static_cast<float>(tick - static_cast<int64_t>(aInterpolation.PreviousFrame.Tick)));
            ++SyncDiagnostics::VehicleApplies;
            //ForceTransform(vehicle, transform, true);

            aInterpolation.PreviousFrame = InterpolationComponent::Timepoint{position, rot, 0.f, static_cast<uint64_t>(tick)};
        }
    }
    else
    {
        if (!aEntityComponent.Controller)
            ++SyncDiagnostics::NoControllerFrames;

        if (aEntityComponent.Controller)
        {
            const auto speed{Lerp(first.Velocity, second.Velocity, ratio)};
            const auto deltaAngle{DeltaAngle(first.Rotation.z, second.Rotation.z, true) * ratio};
            const auto direction = Mod(first.Rotation.z + deltaAngle, 2.f * static_cast<float>(Pi));

            const auto pos = Red::Vector4{position.x, position.y, position.z, 0.f};
            aEntityComponent.Controller->SetTransform(pos, direction, speed);
            ++SyncDiagnostics::CharacterApplies;

            aInterpolation.PreviousFrame = InterpolationComponent::Timepoint{position, glm::vec3{0.f, 0.f, direction}, speed, static_cast<uint64_t>(tick)};
        }
    }
}

void InterpolationSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    spdlog::info("[InterpolationSystem] OnWorldAttached");

    static std::once_flag s_flag;
    std::call_once(s_flag, [this]()
    {
        const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();

        m_entityObserver = pSystem->observer<EntityComponent>()
           .event(flecs::OnAdd)
           .write<InterpolationComponent>()
           .each([](flecs::entity aEntity, EntityComponent&)
           {
               aEntity.add<InterpolationComponent>();
           });
    });

    m_ready = true;
}

void InterpolationSystem::OnAfterWorldDetach()
{
    spdlog::info("[InterpolationSystem] OnAfterWorldDetach");

    m_ready = false;
}

void InterpolationSystem::OnConnected()
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    const float cSimulationDelay = 50.f + (1500.f / pNetworkService->GetServerSettings().get_update_rate());
    spdlog::info("[Sync] interpolation started: {:.0f} ms behind the server clock, update rate {}", cSimulationDelay,
                 pNetworkService->GetServerSettings().get_update_rate());

    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto pMoveSystem = Red::GetGameSystem<RED4ext::vehicle::IMoveSystem>();

    m_interpolator = pSystem->system<const EntityComponent, InterpolationComponent>().each(
        [cSimulationDelay, pMoveSystem](flecs::entity aEntity, const EntityComponent& aEntityComponent, InterpolationComponent& aInterpolation)
        {
            InterpolateEntity(aEntity, aEntityComponent, aInterpolation, cSimulationDelay, pMoveSystem);
        });
}

void InterpolationSystem::OnDisconnected()
{
    if (m_interpolator)
        m_interpolator.destruct();
}

void InterpolationSystem::HandleNotifyEntityMove(const PacketEvent<server::NotifyEntityMove>& aMessage)
{
    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto entity = pSystem->GetEntityByServerId(aMessage.get_id());

    const glm::vec3 position{aMessage.get_position().get_x(), aMessage.get_position().get_y(), aMessage.get_position().get_z()};
    glm::vec3 rotation{0.f, 0.f, 0.f};

    if (aMessage.has_full_rotation())
        rotation = {aMessage.get_full_rotation().get_x(), aMessage.get_full_rotation().get_y(), aMessage.get_full_rotation().get_z()};
    else
        rotation = {0.f, 0.f, aMessage.get_rotation()};

    ++SyncDiagnostics::MovesReceived;
    SyncDiagnostics::LastReceivedTick = aMessage.get_tick();

    if (!entity)
    {
        if (SyncDiagnostics::MovesUnknownEntity++ == 0 || SyncDiagnostics::Every("move-unknown", std::chrono::seconds(10)))
            spdlog::warn("[Sync] move for server id {} which is not spawned here (dropped)", aMessage.get_id());
        return;
    }

    // Moves can arrive while the remote character is still spawning, before the observer has given it an
    // InterpolationComponent (flecs 4 get_mut returns null then). ensure adds it, so early moves are kept too.
    auto& interpolation = entity.ensure<InterpolationComponent>();

    const auto now = std::chrono::steady_clock::now();
    if (interpolation.Received == 0)
        spdlog::info("[Sync] first move for server id {} at ({:.1f}, {:.1f}, {:.1f}) tick {} (local tick {})",
                     aMessage.get_id(), position.x, position.y, position.z, aMessage.get_tick(), NetworkWorldSystem::GetTick());
    else if (now - interpolation.LastReceived > std::chrono::seconds(1))
        spdlog::warn("[Sync] no moves for server id {} for {} ms", aMessage.get_id(),
                     std::chrono::duration_cast<std::chrono::milliseconds>(now - interpolation.LastReceived).count());
    interpolation.LastReceived = now;
    ++interpolation.Received;

    if (!interpolation.TimePoints.empty() && interpolation.TimePoints.back().Tick > aMessage.get_tick())
    {
        ++SyncDiagnostics::MovesOutOfOrder;
        return;
    }

    interpolation.TimePoints.push_back(InterpolationComponent::Timepoint{position, rotation, aMessage.get_speed(), aMessage.get_tick()});
}

static Core::RawFunc<4018412273UL, float (*)(Red::move::Component*, MultiMovementController*)> AttachController;

void (*RealIdleController_SetAnimation)(Game::Controller*, AnimationData&);
void HookIdleController_SetAnimation(Game::Controller* apController, AnimationData& data)
{
    ++SyncDiagnostics::ControllerHookCalls;
    auto* pMoveComponent = apController->MoveComponent;
    Game::EntityPtr entity(pMoveComponent);
    if (const auto pOwner = Red::Cast<Red::GameObject>(entity.GetValuePtr()))
    {
        if (pOwner->tags.Contains(Red::CName("CyberpunkMP.Puppet")))
        {
            if (apController->m_type == MultiMovementController::kMulti)
                return;

            ThreadService::RunInMainThread([id = pOwner->entityID, pMoveComponent, apController]
            {
                const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
                const auto entityQuery = pSystem->query<const EntityComponent>();
                auto flecsEntity = entityQuery.find([id](const EntityComponent& component) { return component.Id == id; });

                auto* pController = Red::Memory::New<MultiMovementController>();
                AttachController(pMoveComponent, pController);
                ++SyncDiagnostics::ControllersAttached;
                spdlog::info("[Sync] movement controller attached to remote character {}", id.hash);

                if (flecsEntity)
                {
                    flecsEntity.get_mut<EntityComponent>()->Controller = pController;
                    spdlog::info("[Sync]   -> given to server id {} (spawned)", flecsEntity.id());
                }
                else
                {
                    const auto spawnQuery = pSystem->query<const SpawningComponent>();
                    flecsEntity = spawnQuery.find([id](const SpawningComponent& component) { return component.Id == id; });
                    if (flecsEntity)
                    {
                        flecsEntity.get_mut<SpawningComponent>()->Controller = pController;
                        spdlog::info("[Sync]   -> given to server id {} (still spawning)", flecsEntity.id());
                    }
                    else
                    {
                        ++SyncDiagnostics::ControllerOrphans;
                        spdlog::warn("[Sync]   -> no tracked entity has game id {}: controller is not used", id.hash);
                    }
                }
            });

            return;
        }
    }

    RealIdleController_SetAnimation(apController, data);
}


void InterpolationSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    Hook<Game::IdleController_SetAnimation>(&HookIdleController_SetAnimation, &RealIdleController_SetAnimation);
    spdlog::info("[Sync] idle-controller hook requested (remote characters get their movement controller through it)");

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&InterpolationSystem::HandleNotifyEntityMove>(this);
}
