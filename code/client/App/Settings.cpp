#include "Settings.h"
#include <fstream>
#include <RED4ext/LaunchParameters.hpp>
#include "App/Debugging/CrashHandler.h"

extern std::filesystem::path GCyberpunkMpLocation;

namespace
{
constexpr auto SessionFileName = "cpmp-session.json";
// A session file older than this is left over from an earlier run and ignored.
constexpr auto SessionFileMaxAge = std::chrono::minutes(15);

// The launcher writes cpmp-session.json next to the mod right before it starts the game. It carries the same
// information as the command line, because games started through Steam (which the Steam overlay and invites need,
// and the only way under Proton) do not reliably receive the launcher's arguments.
void LoadSessionFile(Settings& aSettings, bool aHasCommandLineAddress)
{
    const auto file = GCyberpunkMpLocation / SessionFileName;

    std::error_code ec;
    if (!fs::exists(file, ec))
        return;

    const auto age = fs::file_time_type::clock::now() - fs::last_write_time(file, ec);
    if (ec || age > SessionFileMaxAge)
    {
        spdlog::info("[Session] ignoring stale {}", file.string());
        fs::remove(file, ec);
        return;
    }

    try
    {
        std::ifstream in(file);
        const auto json = nlohmann::json::parse(in);

        aSettings.enabled = true;
        if (!aHasCommandLineAddress)
        {
            aSettings.ip = json.value("ip", std::string(aSettings.ip.c_str())).c_str();
            aSettings.port = json.value("port", aSettings.port);
        }
        aSettings.host = json.value("host", false);
        aSettings.publicAddress = json.value("publicAddress", std::string()).c_str();
        aSettings.waitForInvite = json.value("waitForInvite", false);
        aSettings.autoConnect = aSettings.host || !aSettings.waitForInvite;

        spdlog::info("[Session] {} (server {}:{}, public address '{}')",
                     aSettings.host ? "hosting" : (aSettings.waitForInvite ? "waiting for a Steam invite" : "joining"),
                     aSettings.ip.c_str(), aSettings.port, aSettings.publicAddress.c_str());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[Session] cannot read {}: {}", file.string(), e.what());
    }

    // One use only: the next normal start of the game must not end up in multiplayer.
    fs::remove(file, ec);
}
constexpr auto ModSettingsFileName = "settings.json";

// Persistent mod options (unlike the one-use session file).
void LoadModSettings(Settings& aSettings)
{
    const auto file = GCyberpunkMpLocation / ModSettingsFileName;
    std::error_code ec;
    if (!fs::exists(file, ec))
        return;
    try
    {
        std::ifstream in(file);
        const auto json = nlohmann::json::parse(in);
        aSettings.debugMode = json.value("debugMode", false);
        if (json.contains("options") && json["options"].is_object())
        {
            for (const auto& [name, value] : json["options"].items())
            {
                if (value.is_number())
                    aSettings.options[name] = value.get<float>();
            }
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error("[Settings] cannot read {}: {}", file.string(), e.what());
    }
}

void SaveModSettings(const Settings& aSettings)
{
    const auto file = GCyberpunkMpLocation / ModSettingsFileName;
    nlohmann::json json = nlohmann::json::object();
    try
    {
        std::ifstream in(file);
        if (in)
            json = nlohmann::json::parse(in);
    }
    catch (...)
    {
    }
    json["debugMode"] = aSettings.debugMode;
    json["options"] = nlohmann::json::object();
    for (const auto& [name, value] : aSettings.options)
        json["options"][name] = value;
    std::ofstream out(file);
    out << json.dump(2);
}

// --debug-mode:1, --debug-mode=1, --debug-mode 1 (and 0 to force it off). Read from the raw command line, so it works
// whichever way the game's own parser splits it; it also works in Steam's launch options.
std::optional<bool> DebugModeLaunchOption()
{
    const wchar_t* line = GetCommandLineW();
    if (!line)
        return std::nullopt;
    const std::wstring text(line);
    const std::wstring key = L"-debug-mode";
    const auto pos = text.find(key);
    if (pos == std::wstring::npos)
        return std::nullopt;
    for (size_t i = pos + key.size(); i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (c == L':' || c == L'=' || c == L' ' || c == L'"')
            continue;
        if (c == L'1')
            return true;
        if (c == L'0')
            return false;
        break;
    }
    return true; // bare --debug-mode
}
} // namespace

static const std::unordered_map<std::string, float>& DefaultOptions()
{
    static const std::unordered_map<std::string, float> s_defaults{
        {"partyPanels", 1.f},   {"toasts", 1.f},      {"nameplates", 1.f},    {"nameplateDistance", 300.f},
        {"nameplateShowDistance", 1.f}, {"mapMarkers", 1.f}, {"pingIndicator", 1.f}, {"infoPanel", 1.f},
        {"partyPings", 1.f},    {"chatOpacity", 1.f}, {"chatScale", 1.f}};
    return s_defaults;
}

float Settings::Option(const std::string& acName)
{
    const auto& settings = Get();
    if (acName == "debugMode")
        return settings.debugMode ? 1.f : 0.f;
    if (const auto it = settings.options.find(acName); it != settings.options.end())
        return it->second;
    const auto& defaults = DefaultOptions();
    const auto it = defaults.find(acName);
    return it != defaults.end() ? it->second : 0.f;
}

void Settings::SetOption(const std::string& acName, float aValue)
{
    auto& settings = Get();
    if (acName == "debugMode")
    {
        const bool enabled = aValue >= 0.5f;
        if (settings.debugModeFromLaunchOption)
        {
            if (enabled != settings.debugMode)
                spdlog::info("[Settings] debug mode stays {} for this run (set by the launch option)",
                             settings.debugMode ? "on" : "off");
            return;
        }
        if (enabled != settings.debugMode)
            SetDebugMode(enabled);
        return;
    }
    if (Option(acName) == aValue)
        return;
    settings.options[acName] = aValue;
    spdlog::info("[Settings] {} = {}", acName, aValue);
    SaveModSettings(settings);
}

void Settings::ApplyLogLevel()
{
    spdlog::set_level(Get().debugMode ? spdlog::level::trace : spdlog::level::info);
}

void Settings::SetDebugMode(bool aEnabled)
{
    auto& settings = Get();
    settings.debugMode = aEnabled;
    SaveModSettings(settings);
    ApplyLogLevel();
    spdlog::info("[Settings] debug mode {}{}", aEnabled ? "on" : "off",
                 settings.debugModeFromLaunchOption ? " (the launch option still decides at the next start)" : "");
}

bool Settings::ApplyConnectString(const std::string& acConnect)
{
    // Same flags the launcher passes: --online --ip=<address> --port=<port>, plus --steam-host=<SteamID> from a host
    // that accepts friends through the Steam relay (preferred: no port forwarding).
    std::string ip;
    uint16_t port = 0;
    uint64_t steamHost = 0;

    std::istringstream stream(acConnect);
    std::string token;
    while (stream >> token)
    {
        if (token.rfind("--ip=", 0) == 0)
            ip = token.substr(5);
        else if (token.rfind("--port=", 0) == 0)
            port = static_cast<uint16_t>(std::strtoul(token.c_str() + 7, nullptr, 10));
        else if (token.rfind("--steam-host=", 0) == 0)
            steamHost = std::strtoull(token.c_str() + 13, nullptr, 10);
        else if (token.rfind("--lobby=", 0) == 0)
            Get().lobby = std::strtoull(token.c_str() + 8, nullptr, 10);
    }

    const bool hasAddress = !ip.empty() && port != 0;
    if (!hasAddress && steamHost == 0)
        return false;

    if (hasAddress)
    {
        Get().ip = ip.c_str();
        Get().port = port;
    }
    Get().steamHost = steamHost;
    return true;
}

void Settings::Load()
{
    Settings& settings = Get();

    LoadModSettings(settings);
    if (const auto option = DebugModeLaunchOption())
    {
        settings.debugMode = *option;
        settings.debugModeFromLaunchOption = true;
    }
    ApplyLogLevel();
    spdlog::info("[Settings] debug mode {} ({})", settings.debugMode ? "on" : "off",
                 settings.debugModeFromLaunchOption ? "launch option" : "settings.json");

    auto& launchParameters = RED4ext::GetLaunchParameters();

    if (launchParameters.Contains(RED4ext::CString("-online")))
        settings.enabled = true;

    bool hasCommandLineAddress = false;
    if (const auto ip = launchParameters.Get("-ip"); ip)
    {
        hasCommandLineAddress = ip->Size() > 0;
        if (ip->Size() > 0)
            settings.ip = (*ip)[0].c_str();
    }

    if (const auto port = launchParameters.Get("-port"); port)
    {
        if (port->Size() > 0)
            settings.port = std::strtoul((*port)[0].c_str(), nullptr, 10) & 0xFFFF;
    }

    if (const auto steamHost = launchParameters.Get("-steam-host"); steamHost)
    {
        if (steamHost->Size() > 0)
        {
            settings.steamHost = std::strtoull((*steamHost)[0].c_str(), nullptr, 10);
            hasCommandLineAddress = hasCommandLineAddress || settings.steamHost != 0;
        }
    }

    if (const auto lobby = launchParameters.Get("-lobby"); lobby)
    {
        if (lobby->Size() > 0)
            settings.lobby = std::strtoull((*lobby)[0].c_str(), nullptr, 10);
    }

    if (const auto mods = launchParameters.Get("-mod"); mods)
    {
        for (const auto& mod : *mods)
            settings.mods.push_back(mod.c_str());
    }

    if (launchParameters.Contains("-cpmp-debug"))
    {
        settings.debugMenu = true;
    }

    if (launchParameters.Contains("-rpc"))
    {
        settings.RpcOnly = true;
    }

    if (const auto rpcDir = launchParameters.Get("-rpcdir"); rpcDir)
    {
        if (rpcDir->Size() > 0)
        {
            // For some reason cyberpunk adds a \ at the start and end of the path...
            std::string path = std::string((*rpcDir)[0].c_str());
            path = path.substr(1, path.length() - 2);

            settings.RpcPath = path;
        }
    }

    // Started with an address (e.g. by Steam after accepting an invite while the game was closed).
    if (hasCommandLineAddress && settings.enabled)
        settings.autoConnect = true;

    LoadSessionFile(settings, hasCommandLineAddress);

    App::CrashHandler::LogEnvironment();
}
