#pragma once

#include <RED4ext/Scripting/Natives/Generated/red/Event.hpp>
#include <RED4ext/CString.hpp>
#include "Red/TypeInfo/Macros/Definition.hpp"

struct ChatMessageUIEvent : RED4ext::red::Event
{
    // Own RTTI names, otherwise it inherits redEvent's and gets registered over the game's Event type.
    static constexpr const char* NAME = "ChatMessageUIEvent";
    static constexpr const char* ALIAS = "ChatMessageUIEvent";

    RED4ext::CString author;
    RED4ext::CString message;

    RTTI_IMPL_TYPEINFO(ChatMessageUIEvent);
    RTTI_IMPL_ALLOCATOR();
};

RTTI_DEFINE_CLASS(ChatMessageUIEvent, { 
    RTTI_PARENT(RED4ext::red::Event);
    // auto pType = Red::GetClass("String");
    // type->props.PushBack(Red::CProperty::Create(pType, "author", nullptr, static_cast<uint32_t>(0x40)));
    // type->props.PushBack(Red::CProperty::Create(pType, "message", nullptr, static_cast<uint32_t>(0x60)));
    RTTI_PROPERTY(author);
    RTTI_PROPERTY(message);
});