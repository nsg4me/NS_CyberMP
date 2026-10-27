#pragma once

// Writes crashes into CyberpunkMP.log (exception, faulting module + offset, call stack) and saves a small crash dump in
// <mod>/crashes/, so a crash can be traced even when the game's own reporter shows nothing useful.
namespace App::CrashHandler
{
void Install();

// Startup inventory: game version, loaded RED4ext plugins, CET, mod counts. Logged once per session.
void LogEnvironment();
}
