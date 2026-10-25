#pragma once

// Layouts of the game's compiled-script bundle, used to validate/generate RPC classes.
// Taken from Codeware (MIT, psiberx) which keeps them current; ScriptDefinition grew by 0x28 after 2.2x, which moved
// every field below compared to the original CyberpunkMP copy.

#include <RED4ext/CName.hpp>
#include <RED4ext/Containers/DynArray.hpp>
#include <RED4ext/HashMap.hpp>
#include <RED4ext/Map.hpp>
#include <RED4ext/Memory/Allocators.hpp>
#include <RED4ext/RTTITypes.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#include <RED4ext/Scripting/Functions.hpp>

namespace Game
{

enum class EScriptDefinition : uint8_t
{
    Type = 0,
    Class,
    Constant, // Values for Enums and Bitfields
    Enum,
    Bitfield,
    Function,
    Parameter,
    LocalVar,
    Property,
    SourceFile,
};

enum class EScriptVisibility : uint8_t
{
    Public = 0,
    Protected,
    Private,
};

struct ScriptDefinition
{
    virtual RED4ext::Memory::IAllocator* GetAllocator() = 0; // 00
    virtual ~ScriptDefinition() = 0;                // 08
    virtual EScriptDefinition GetType() = 0;        // 10
    virtual EScriptVisibility GetVisibility() = 0;  // 18
    virtual ScriptDefinition* GetParent() = 0;      // 20
    virtual RED4ext::CName GetName() = 0;                    // 28
    virtual bool IsNative() = 0;                    // 30

    RED4ext::CName name;          // 08
    uint64_t unk10;      // 10
    uint8_t unk18[0x28]; // 18
};

struct ScriptFileInfo : ScriptDefinition
{
    uint32_t crc;   // 40
    uint32_t index; // 44
    uint32_t hash;  // 48 - Murmur3 of path
    RED4ext::CString path;   // 50
};

enum class EScriptType : uint8_t
{
    Primitive = 0,
    Class, // Class/Enum/Bitfield
    StrongHandle,
    WeakHandle,
    DynArray,
    StaticArray,
    ScriptRef,
};

struct ScriptType : ScriptDefinition
{
    RED4ext::CBaseRTTIType* rttiType;     // 40
    ScriptDefinition* innerType; // 48
    int32_t arraySize;           // 50
    EScriptType metaType;        // 54
};
RED4EXT_ASSERT_OFFSET(ScriptType, innerType, 0x48);
RED4EXT_ASSERT_OFFSET(ScriptType, metaType, 0x54);

struct ScriptProperty : ScriptDefinition
{
    struct Flags
    {
        uint16_t isNative : 1;     // 00
        uint16_t b01 : 1;          // 01
        uint16_t b02 : 1;          // 02
        uint16_t isConst : 1;      // 03
        uint16_t b04 : 1;          // 04
        uint16_t b05 : 1;          // 05
        uint16_t isPersistent : 1; // 06
        uint16_t b07 : 1;          // 07
        uint16_t b08 : 1;          // 08
    };

    RED4ext::CProperty* rttiProp;                // 40
    ScriptDefinition* parent;           // 48
    Flags flags;                        // 50
    RED4ext::Map<RED4ext::CString, RED4ext::CString> defaults;     // 58
    EScriptVisibility visibility;       // 80
    RED4ext::HashMap<RED4ext::CName, RED4ext::CString> attributes; // 88
    ScriptType* type;                   // B8
};
RED4EXT_ASSERT_OFFSET(ScriptProperty, type, 0xB8);

struct ScriptClass;
struct ScriptFunction;

struct ScriptParameter : ScriptDefinition
{
    struct Flags
    {
        uint8_t isOptional : 1;     // 00
        uint8_t isOut : 1;          // 01
        uint8_t isShortCircuit : 1; // 02 - Got from redscript
        uint8_t isConst : 1;        // 03
    };

    RED4ext::CProperty* rttiProp;      // 40
    ScriptFunction* function; // 48
    ScriptType* type;         // 50
    Flags flags;              // 58
};

struct ScriptLocalVar : ScriptDefinition
{
    RED4ext::CProperty* rttiProp;      // 40
    ScriptFunction* function; // 48
    ScriptType* type;         // 50
    uint8_t unk58;            // 58
};

struct ScriptFunction : ScriptDefinition
{
    struct Flags
    {
        uint32_t isStatic : 1;     // 00
        uint32_t isExec : 1;       // 01
        uint32_t isTimer : 1;      // 02
        uint32_t isFinal : 1;      // 03
        uint32_t isNative : 1;     // 04
        uint32_t b05 : 1;          // 05
        uint32_t b06 : 1;          // 06
        uint32_t isEvent : 1;      // 07
        uint32_t b08 : 1;          // 08
        uint32_t b09 : 1;          // 09
        uint32_t b10 : 1;          // 0A
        uint32_t b11 : 1;          // 0B
        uint32_t b12 : 1;          // 0C
        uint32_t b13 : 1;          // 0D
        uint32_t b14 : 1;          // 0E
        uint32_t isConst : 1;      // 0F
        uint32_t isThreadsafe : 1; // 10
        uint32_t isScripted : 1;   // 11 - Seems pointless, for all functions isScripted != isNative
    };

    RED4ext::CBaseFunction* rttiFunc;             // 40
    ScriptClass* parent;                 // 48
    RED4ext::CName shortName;                     // 50
    RED4ext::DynArray<ScriptParameter*> params;   // 58
    RED4ext::DynArray<ScriptLocalVar*> localVars; // 68
    RED4ext::HashMap<RED4ext::CName, RED4ext::CString> attributes;  // 78
    ScriptType* returnType;              // A8
    uint16_t unkB0;                      // B0
    ScriptFunction* overridden;          // B8
    Flags flags;                         // C0
    RED4ext::CName operatorName;                  // C8
    uint8_t castValue;                   // D0
    EScriptVisibility visibility;        // D1
    RED4ext::DynArray<uint8_t> code;              // D8
    ScriptFileInfo* sourceFile;          // E8
    uint32_t sourceLine;                 // F0
};

struct ScriptClass : ScriptDefinition
{
    struct Flags
    {
        uint8_t isNative : 1;     // 00
        uint8_t isImportOnly : 1; // 01
        uint8_t isAbstract : 1;   // 02
        uint8_t isFinal : 1;      // 03
        uint8_t isStruct : 1;     // 04
    };

    RED4ext::CClass* rttiClass;                    // 40
    ScriptClass* parent;                  // 48
    RED4ext::DynArray<ScriptProperty*> properties; // 50
    RED4ext::DynArray<ScriptProperty*> overrides;  // 60
    uint8_t unk70[0x20];                  // 70
    RED4ext::DynArray<ScriptFunction*> functions;  // 90
    RED4ext::HashMap<RED4ext::CName, RED4ext::CString> attributes;   // A0
    EScriptVisibility visibility;         // D0
    Flags flags;                          // D1
};
RED4EXT_ASSERT_OFFSET(ScriptClass, parent, 0x48);
RED4EXT_ASSERT_OFFSET(ScriptClass, properties, 0x50);
RED4EXT_ASSERT_OFFSET(ScriptClass, overrides, 0x60);
RED4EXT_ASSERT_OFFSET(ScriptClass, functions, 0x90);
RED4EXT_ASSERT_OFFSET(ScriptClass, flags, 0xD1);

struct ScriptConstant : ScriptDefinition
{
    int64_t rttiValue;        // 40 - Used only for natives
    ScriptDefinition* parent; // 48 - Enum or Bitfield
    int64_t value;            // 50
    bool isNative;            // 58
};

struct ScriptEnum : ScriptDefinition
{
    RED4ext::CEnum* rttiEnum;                     // 40
    RED4ext::DynArray<ScriptConstant*> constants; // 48
    uint8_t size;                        // 58
    bool isNative;                       // 59
    EScriptVisibility visibility;        // 5A
};

struct ScriptBitfield : ScriptDefinition
{
    RED4ext::CBitfield* rttiBitfield;             // 40
    RED4ext::DynArray<ScriptConstant*> constants; // 48
    uint8_t size;                        // 58
    bool isNative;                       // 59
    EScriptVisibility visibility;        // 5A
};

struct ScriptBundle
{
    void* unk00;                                         // 00
    RED4ext::HashMap<RED4ext::CName, ScriptDefinition*> definitionsByName; // 08
    RED4ext::HashMap<RED4ext::CName, ScriptFunction*> functionsByName;     // 38
    RED4ext::HashMap<RED4ext::CName, ScriptType*> typesByName;             // 68
    RED4ext::DynArray<ScriptFileInfo*> files;                     // 98
    RED4ext::DynArray<ScriptFunction*> globals;                   // A8
    RED4ext::DynArray<ScriptEnum*> enums;                         // B8
    RED4ext::DynArray<ScriptBitfield*> bitfields;                 // C8
    RED4ext::DynArray<ScriptClass*> classes;                      // D8
    RED4ext::DynArray<ScriptType*> types;                         // E8
    RED4ext::DynArray<RED4ext::CString> strings;                           // F8
};
RED4EXT_ASSERT_OFFSET(ScriptBundle, definitionsByName, 0x08);
RED4EXT_ASSERT_OFFSET(ScriptBundle, classes, 0xD8);

struct StringView
{
    const char* str;
    uint32_t length;
};

// The validator's error report. Only the first field after the vtable is touched.
struct ScriptReport
{
    virtual ~ScriptReport() = default;
    virtual void AddValidationError(StringView&) = 0;

    bool fillErrors;
};
} // namespace Game
