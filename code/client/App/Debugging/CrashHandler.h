#pragma once

// Writes crashes into CyberpunkMP.log (exception, faulting module + offset, call stack) and saves a small crash dump in
// <mod>/crashes/, so a crash can be traced even when the game's own reporter shows nothing useful.
namespace App::CrashHandler
{
void Install();

// Startup inventory: game version, loaded RED4ext plugins, CET, mod counts. Logged once per session.
void LogEnvironment();

// Installed content mods that can add items or clothes (archives, tweaks), as sorted lowercase relative paths.
// Compared with the host's to explain missing clothes. Cached after the first call.
const std::vector<std::string>& ContentMods();
}
