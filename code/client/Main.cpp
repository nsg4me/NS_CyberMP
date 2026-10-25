#include <shellapi.h>
#include <ArchiveXL/support/red4ext/ArchiveXL.hpp>
#include <TweakXL/support/red4ext/TweakXL.hpp>
#include <App/Settings.h>
#include <RED4ext/LaunchParameters.hpp>

#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Runtime.hpp>
#include <RED4ext/Api/v1/Version.hpp>
#include <RED4ext/Api/ApiVersion.hpp>

extern void CoreStubsInit();

std::filesystem::path GCyberpunkMpLocation;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

void Initialize()
{
    namespace fs = std::filesystem;

    constexpr auto pathLength = MAX_PATH + 1;
    std::string filename;
    do
    {
        filename.resize(filename.size() + pathLength, '\0');

        const auto length = GetModuleFileName((HINSTANCE)&__ImageBase, filename.data(), static_cast<uint32_t>(filename.size()));
        if (length > 0)
        {
            filename.resize(length);
        }
    } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    fs::path path = filename;
    if (is_symlink(path))
    {
        path = read_symlink(path);
    }

    GCyberpunkMpLocation = path.parent_path();
}

#include "App/Application.h"

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason, const RED4ext::v1::Sdk* aSdk)
{

    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        Initialize();

        App::GApplication = MakeUnique<App::Application>(aHandle, aSdk);
        App::GApplication->Bootstrap();

        const auto scriptPath = GCyberpunkMpLocation / TP_REDSCRIPT_LOCATION;
        const wchar_t* scriptLocation = canonical(scriptPath).c_str();
        aSdk->scripts->Add(aHandle, scriptLocation);

        ArchiveXL::RegisterArchives(canonical(GCyberpunkMpLocation / TP_ARCHIVES_LOCATION));
        TweakXL::RegisterTweaks(canonical(GCyberpunkMpLocation / TP_TWEAKS_LOCATION));

        const auto inputLoaderModule = LoadLibrary("input_loader");
        void(*pInputLoaderAdd)(RED4ext::v1::PluginHandle, const wchar_t*);
        if (inputLoaderModule != nullptr && (pInputLoaderAdd = reinterpret_cast<decltype(pInputLoaderAdd)>(GetProcAddress(inputLoaderModule, "Add"))))
        {
            const wchar_t* input_path = canonical(GCyberpunkMpLocation / TP_INPUTS_LOCATION).c_str();
            pInputLoaderAdd(aHandle, input_path);
        }
        else
        {
            const auto message =
                L"The following CyperpunkMP requirements were not met:\n\n* Input Loader v0.2.0\nPlease ensure the mods "
                L"above are installed/up-to-date.";
            MessageBoxW(nullptr, message, L"CyperpunkMP requirements could not be found", MB_SYSTEMMODAL | MB_ICONERROR);
            return false;
        }

        for (auto& mod : Settings::Get().mods)
        {
            aSdk->scripts->Add(aHandle, mod.wstring().c_str());
            ArchiveXL::RegisterArchives(mod);
            TweakXL::RegisterTweaks(mod);
            pInputLoaderAdd(aHandle, mod.wstring().c_str());
        }

        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        if (Settings::IsDisabled())
            return false;

        App::GApplication->Shutdown();
        App::GApplication = nullptr;
        break;
    }
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"CyberpunkMP";
    aInfo->author = L"Tilted Phoques SRL";
    aInfo->version = RED4EXT_V1_SEMVER(0, 1, 0);

    // Struct layouts from the SDK are only valid for the game version it was generated for, so refuse to load on any
    // other version instead of crashing at runtime.
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
