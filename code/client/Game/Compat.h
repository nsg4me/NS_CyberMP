#pragma once

// Game internals that the upstream RED4ext.SDK does not describe.
//
// CyberpunkMP originally shipped its own RED4ext.SDK fork with these layouts reverse-engineered for game 2.2x. The
// port to 2.5 uses the upstream SDK unmodified, and everything the fork added that we still need lives here instead,
// so it is in ONE place when a patch moves something.
//
// Each item is tagged:
//   [robust]    found by name/type at runtime (RTTI, component lookup) - survives patches
//   [same]      offset unchanged between 2.2x and 2.5 (its neighbours in the upstream 2.5 SDK did not move)
//   [likely]    moved in 2.5, new value derived from how the surrounding block moved in the upstream 2.5 SDK
//   [VERIFY]    2.2x value, may have moved in 2.5 - check against the game before relying on it
//
// [VERIFY] writes are compiled out unless CPMP_UNVERIFIED_OFFSETS is defined: a wrong offset on a read gives a bad
// value, but a wrong offset on a write corrupts memory and crashes somewhere unrelated.

#include <mutex>
#include <set>
#include <string>

#include <RED4ext/Scripting/Natives/Generated/Box.hpp>
#include <RED4ext/Scripting/Natives/Generated/Matrix.hpp>
#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector3.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldTransform.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/ui/CharacterCustomizationState.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/TextInputWidget.hpp>
#include <RED4ext/Scripting/Natives/Generated/move/Component.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>
#include <RED4ext/Scripting/Natives/vehicleBaseObject.hpp>

namespace Game::Compat
{
// Script-visible calls return false when the function is missing or its signature changed. Report each one once so a
// patch that renames something shows up in the log instead of silently doing nothing.
inline void WarnOnce(bool aOk, const char* aWhat)
{
    static std::mutex s_lock;
    static std::set<std::string> s_reported;
    if (aOk)
        return;
    std::scoped_lock lock(s_lock);
    if (s_reported.insert(aWhat).second)
        spdlog::warn("[Compat] script call {} failed (missing or changed in this game version)", aWhat);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------------------------------

// [robust] The fork exposed gamePuppet::moveComponent at a fixed offset; look it up among the entity's components.
inline Red::move::Component* GetMoveComponent(Red::ent::Entity* apEntity)
{
    if (!apEntity)
        return nullptr;

    static auto* s_type = Red::GetClass<Red::move::Component>();
    for (auto& component : apEntity->components)
    {
        if (component && component->GetType()->IsA(s_type))
            return reinterpret_cast<Red::move::Component*>(component.instance);
    }

    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// moveComponent
// ---------------------------------------------------------------------------------------------------------------------

struct MovementRepresentation
{
    Red::move::Component* parent; // 00
    bool active;                  // 08
    Red::DynArray<void*> stack;   // 10
    int32_t activeIndex;          // 20
    void* activeEntry;            // 28
};
RED4EXT_ASSERT_SIZE(MovementRepresentation, 0x30);

// [same] 0x138 in 2.2x. moveComponent kept its size (0x2C0) and its known fields (speed at 0x220) in 2.5.
constexpr uintptr_t MoveComponent_Representation = 0x138;

inline MovementRepresentation& GetRepresentation(Red::move::Component* apComponent)
{
    return *reinterpret_cast<MovementRepresentation*>(reinterpret_cast<uintptr_t>(apComponent) +
                                                      MoveComponent_Representation);
}

// ---------------------------------------------------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------------------------------------------------

// [same] 0x260 in 2.2x. isOnGround (0x25C) and acceleration (0x264) did not move in 2.5.
constexpr uintptr_t Vehicle_PhysicsState = 0x260;

inline uint32_t GetPhysicsState(Red::vehicle::BaseObject* apVehicle)
{
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(apVehicle) + Vehicle_PhysicsState);
}

// [robust] The fork read the transform from vehicle->rigidBody->currentTransform. The placed component carries the
// same world transform (the original authors noted "about the same") without depending on the physics layout.
inline Red::WorldTransform GetWorldTransform(Red::vehicle::BaseObject* apVehicle)
{
    if (apVehicle && apVehicle->transformComponent)
        return apVehicle->transformComponent->worldTransform;
    return {};
}

// [robust] Speed through the script-visible VehicleObject.GetCurrentSpeed() instead of rigidBody->velocity.
inline float GetSpeed(Red::vehicle::BaseObject* apVehicle)
{
    float speed = 0.f;
    if (apVehicle)
        WarnOnce(Red::CallVirtual(apVehicle, "GetCurrentSpeed", speed), "VehicleObject.GetCurrentSpeed");
    return std::abs(speed);
}

// [robust] The fork called a raw vtable slot (+0x328) to start the engine; use the script-visible function instead.
inline void TurnEngineOn(Red::vehicle::BaseObject* apVehicle, bool aOn)
{
    if (apVehicle)
        WarnOnce(Red::CallVirtual(apVehicle, "TurnEngineOn", aOn), "VehicleObject.TurnEngineOn");
}

// [VERIFY] vehicleWheeledBaseObject::engineData. 2.2x: 0xBA0 (base object ended at 0xB90).
// In 2.5 the base object grew to 0xBC0, so this is most likely 0xBD0.
constexpr uintptr_t WheeledVehicle_EngineData = 0xBD0;
constexpr uintptr_t Engine_Unk61 = 0x61;

inline void ClearEngineFlag61(Red::vehicle::BaseObject* apWheeledVehicle)
{
#ifdef CPMP_UNVERIFIED_OFFSETS
    auto* engine = *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(apWheeledVehicle) +
                                                WheeledVehicle_EngineData);
    if (engine)
        engine[Engine_Unk61] = 0;
#else
    (void)apWheeledVehicle;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Character customization
// ---------------------------------------------------------------------------------------------------------------------

namespace EntityAppearanceChangeParameter
{
struct Key
{
    Red::ResourcePath resource; // 00
    Red::CName definition;      // 08
};
RED4EXT_ASSERT_SIZE(Key, 0x10);
} // namespace EntityAppearanceChangeParameter

// [robust] IsBodyGenderMale is script-visible on gameuiICharacterCustomizationState.
inline bool IsBodyGenderMale(Red::game::ui::CharacterCustomizationState* apState)
{
    bool male = false;
    if (apState)
        WarnOnce(Red::CallVirtual(apState, "IsBodyGenderMale", male), "CharacterCustomizationState.IsBodyGenderMale");
    return male;
}

// [VERIFY] Native virtuals of gameuiICharacterCustomizationState that collect the appearance keys for a body part.
// 2.2x slots: GetHead 0x190, GetBody 0x1A0, GetArms 0x1B0. 2.5 grew the state (cyberware customization), so new
// virtuals may have been inserted before these.
constexpr uintptr_t CCState_GetHeadCustomization = 0x190;
constexpr uintptr_t CCState_GetBodyCustomization = 0x1A0;
constexpr uintptr_t CCState_GetArmsCustomization = 0x1B0;

using GetCustomization_t = bool (*)(Red::game::ui::CharacterCustomizationState*, Red::CName, bool,
                                    Red::DynArray<EntityAppearanceChangeParameter::Key>&);

inline bool GetCustomization(uintptr_t aSlot, Red::game::ui::CharacterCustomizationState* apState, Red::CName aGroup,
                             bool aIsTPP, Red::DynArray<EntityAppearanceChangeParameter::Key>& aKeys)
{
#ifdef CPMP_UNVERIFIED_OFFSETS
    auto vft = *reinterpret_cast<uintptr_t*>(apState);
    auto func = *reinterpret_cast<GetCustomization_t*>(vft + aSlot);
    return func(apState, aGroup, aIsTPP, aKeys);
#else
    (void)aSlot;
    (void)apState;
    (void)aGroup;
    (void)aIsTPP;
    (void)aKeys;
    return false;
#endif
}
// ---------------------------------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------------------------------

// [likely] inkTextInputWidget::caretPosition. 2.2x: 0x434 in a 0x40-byte block at 0x430 (widget size 0x470).
// 2.5: the widget is 0x4A0 and its own block starts at 0x460 - same 0x40 size, shifted by 0x30 with its base class.
constexpr uintptr_t TextInputWidget_CaretPosition = 0x464;

inline void SetCaretPosition(Red::ink::TextInputWidget* apWidget, int32_t aPosition)
{
    *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(apWidget) + TextInputWidget_CaretPosition) = aPosition;
}
} // namespace Game::Compat
