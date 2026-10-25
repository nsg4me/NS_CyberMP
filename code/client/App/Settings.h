#pragma once

namespace fs = std::filesystem;

struct Settings
{
    static Settings& Get()
    {
        static Settings instance;
        return instance;
    }
    static bool IsDisabled()
    {
        return !Get().enabled;
    }
    static void Load();

    fs::path exePath{};
    fs::path gamePath{};
    String Version{};
    String ip = "127.0.0.1";
    uint16_t port = 11778;
    Vector<fs::path> mods = {};
    bool enabled = false;
    bool RpcOnly = false;
    // Developer "Test" menu bar (ImGui) at the top of the screen; off unless the game is started with --cpmp-debug.
    bool debugMenu = false;
    fs::path RpcPath{};

    // Set by the launcher's "Host game" (cpmp-session.json): this player runs the server, and friends reach it at
    // publicAddress ("ip:port"), which is advertised to Steam friends as the join address.
    bool host = false;
    String publicAddress{};
    // Joining through a Steam invite: start in multiplayer mode and wait for the invite instead of a fixed address.
    bool waitForInvite = false;

    // Address to connect to, from a Steam join request ("--online --ip=... --port=..." connect string).
    // Returns false when the string has no usable address.
    static bool ApplyConnectString(const std::string& acConnect);

private:
    Settings() = default;
};