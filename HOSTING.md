# Hosting from your PC and inviting Steam friends

The launcher can run the server on your own PC and start the game connected to it. Friends join through Steam
invites. Works on Windows and on Linux (game through Steam/Proton).

## What happens when you press "Host game"

1. The launcher writes `server/config/server.json` (private: never listed publicly) and starts the bundled server.
2. It asks your router to open **UDP 11778** using UPnP and works out your public address. If UPnP is off, it tells
   you to forward that one port to your PC yourself.
3. It starts Cyberpunk through Steam (needed for the overlay and invites, and the only way under Proton) and leaves
   the connection details in `mod/cpmp-session.json`, which the mod reads once at startup.
4. In game, the mod sets your Steam "Join Game" address to your public address.

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

## When friends cannot connect

- The launcher log says whether the router opened the port. If not, forward **UDP 11778** to your PC.
- "Your router's internet address is itself private" means a second router or your ISP's CGNAT: forward the port on
  the outer router too, or ask the ISP for a public IP. A VPN (e.g. WireGuard) also works: friends on
  the VPN join at your VPN address.
- The server has no password support (the protocol never had one); it stays private because it is not listed and
  only people you invite get the address.
