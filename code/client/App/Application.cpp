#include "Application.h"

#include <fstream>

extern std::filesystem::path GCyberpunkMpLocation;

#include "Core/Foundation/LocaleProvider.hpp"
#include "Core/Foundation/RuntimeProvider.hpp"
#include "Network/NetworkService.h"
#include "Network/Rpc/RpcService.h"
#include "Rendering/ImGuiService.h"
#include "Debugging/DebugService.h"
#include "Threading/ThreadService.h"
#include "Steam/SteamService.h"
#include "Debugging/CrashHandler.h"

#include "Support/MinHook/MinHookProvider.hpp"
#include "Support/RedLib/RedLibProvider.hpp"
#include "Support/Renderer/RenderingProvider.hpp"
#include "Support/Spdlog/SpdlogProvider.hpp"


namespace App
{
UniquePtr<Application> GApplication;

Application::Application(RED4ext::v1::PluginHandle aPlugin, const RED4ext::v1::Sdk* aSdk)
    : Core::Application(aPlugin, aSdk)
{
    Register<Core::LocaleProvider>();
    Register<Core::RuntimeProvider>(aPlugin)->SetBaseImagePathDepth(2);

    Register<Support::MinHookProvider>();
    Register<Support::SpdlogProvider>();
    Register<Support::RedLibProvider>();
    Register<Support::RenderingProvider>();

    Register<ThreadService>();
    Register<NetworkService>();
    Register<DebugService>();
    Register<ImGuiService>();
    Register<RpcService>(aPlugin, aSdk);
    Register<SteamService>();
}

void Application::OnStarted()
{
    Core::Application::OnStarted();

    // Session header: enough to tell builds and Proton apart when reading a log later.
    std::string version = "dev";
    {
        std::ifstream in(GCyberpunkMpLocation / "version.txt");
        std::getline(in, version);
    }

    std::string platform = "Windows";
    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll"))
    {
        using WineVersion_t = const char* (*)();
        if (const auto wineVersion = reinterpret_cast<WineVersion_t>(GetProcAddress(ntdll, "wine_get_version")))
            platform = fmt::format("Wine/Proton {}", wineVersion());
    }

    spdlog::info("NS-CyberMP client started: mod {}, {}, mod folder {}", version, platform,
                 GCyberpunkMpLocation.string());

    CrashHandler::Install();
}

void Application::OnStopped()
{
    Core::Application::OnStopped();
}
} // namespace App
