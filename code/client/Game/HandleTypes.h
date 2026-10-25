#pragma once

// Types held by Handle<> members of the game classes CyberpunkMP destroys through Handle<> (game::Object, vehicles,
// puppets...). The generated SDK headers only forward-declare them, and the upstream SDK's Handle<T> destructor
// static_asserts that T is complete, so the full definitions have to be visible in every translation unit.

#include <RED4ext/Scripting/Natives/Generated/AI/Archetype.hpp>
#include <RED4ext/Scripting/Natives/Generated/C2dArray.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/ActionAnimDatabase.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/GenericAnimDatabase.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimationControlBinding.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/SlotComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/ITransformBinding.hpp>
#include <RED4ext/Scripting/Natives/Generated/AI/behavior/ParameterizedBehavior.hpp>
#include <RED4ext/Scripting/Natives/Generated/AI/ArgumentDefinition.hpp>
#include <RED4ext/Scripting/Natives/Generated/AI/behavior/Resource.hpp>
#include <RED4ext/Scripting/Natives/Generated/AI/behavior/BehaviorDelegate.hpp>
#include <RED4ext/Scripting/Natives/Generated/AI/behavior/TreeNodeDefinition.hpp>
