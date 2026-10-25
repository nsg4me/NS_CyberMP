# Hosting from your PC and inviting Steam friends

The launcher can run the server on your own PC and start the game connected to it. Friends join through Steam
invites. Works on Windows and on Linux (game through Steam/Proton).

## What happens when you press "Host game"

1. The launcher writes `server/config/server.json` (private: never listed publicly) and starts the bundled server.
2. It asks your router to open **UDP 11778** using UPnP and works out your public address. This is only a fallback
   for friends joining by address: Steam invites go through the Steam relay and need no open port.
3. It starts Cyberpunk through Steam (needed for the overlay and invites, and the only way under Proton) and leaves
   the connection details in `mod/cpmp-session.json`, which the mod reads once at startup.
4. In game, the mod accepts Steam friends over the Steam relay network and passes their traffic to your local server,
   and sets your Steam "Join Game" info so invites point at you (by SteamID, with the public address as a fallback).
5. Everyone connects automatically once their save has loaded (the "hold /" key still works).

Stopping hosting, or closing the launcher, stops the server and closes the router port.

## Inviting

In game: **Shift+Tab → Friends → right-click a friend → Invite to Game**. Friends can also right-click you and
pick **Join Game**.

- Friend's game already running (started with **Join a friend** in their launcher): accepting connects them.
- Friend's game not running: Steam starts it with the address. If their game still opens without connecting, they
  start it with **Join a friend** first and accept the invite again.
- Friends without Steam (GOG/Epic): give them the address shown in the launcher and they add it as a private server.

Everyone needs the same launcher version (the mod is installed by the launcher).

## Requirements

- Game patch 2.5 with RED4ext 1.31+, Codeware, ArchiveXL, TweakXL, redscript and Input Loader.
- Host: the [.NET 8 runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
  (Garuda/Arch: `sudo pacman -S dotnet-runtime-8.0`).
- Windows host: allow the server through Windows Firewall when it asks.
- Linux (Proton): set the game's Steam launch options to `WINEDLLOVERRIDES="winmm=n,b" %command%` so RED4ext loads.
- Everyone (host and friends): the **latest Visual C++ redistributable** (https://aka.ms/vc14/vc_redist.x64.exe).
  RED4ext and the mod are built with a recent MSVC, and with an older `msvcp140.dll` they crash on their first
  `std::mutex` lock ("No access to memory location" / "RED4ext could not be loaded"). Under Proton, install it into
  the game's prefix: `protontricks-launch --appid 1091500 ~/Downloads/vc_redist.x64.exe`.

## How the Steam relay works

Your game and the server still talk their normal UDP protocol. The mod tunnels it through Steam's networking
(ISteamNetworkingMessages from the game's own Steam API), addressed by SteamID:

    friend's game -> 127.0.0.1 (local tunnel) => Steam relay => your game -> 127.0.0.1:11778 (server)

No ports are opened, it works behind CGNAT, and nobody sees anyone's IP address. Only people on your **Steam friends
list** are accepted.

## When friends cannot connect

- Check `red4ext/plugins/zzzCyberpunkMP/CyberpunkMP.log` on both sides for `[Relay]` lines: "Steam relay network:
  ready", "accepted session from ...", "connected through Steam".
- "refused session ... not on your Steam friends list": add each other as Steam friends.
- Friends joining by address (no Steam): forward **UDP 11778** to your PC; behind CGNAT that will not work.
- The server has no password support (the protocol never had one); it is private because it is not listed and the
  relay only accepts your Steam friends.
