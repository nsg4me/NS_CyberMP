# Developer notes (handoff)

Where the project stands, for picking work up in a new session. Keep this short and current.

## Layout

- `code/client`: the game plugin (RED4ext 1.31, patch 2.5). `App/World/*System` = sync (NetworkWorldSystem owns spawn,
  interpolation, appearance, vehicles, chat). `App/Steam` = Steam relay tunnel + party lobby. `App/Rendering/Overlay` =
  ImGui toasts/party panels. `App/Debugging/CrashHandler` = crash log + dumps, `[Env]` startup inventory.
- `code/assets/redscript`: scripts. `World/Diagnostics.reds` logs game events through `NetworkWorldSystem.Log`.
- `code/server`: native server (flecs) + .NET loader. `native/ServerDiagnostics.h` = per-entity move stats.
- `code/launcher`: Electron launcher (requirements installer, hosting, updater, Collect logs, key binding).
- CI: `.github/workflows/build.yml` (Windows client/server/package, Linux). Releases: create the tag/release on GitHub
  at the latest `port/2.5` commit; CI attaches the assets.

## Checking changes without Windows

- redscript: `redscript-cli compile -s <Codeware/scripts> -s code/assets/redscript -b <vanilla final.redscripts> -o out`
  (catches wrong class/method names in `@wrapMethod`).
- C++: push and let CI (MSVC) compile; symbolize crashes with the `client-dll` artifact's PDB
  (`llvm-symbolizer --obj=CyberpunkMP.dll --relative-address 0x<offset>`).

## Working

Steam-relay hosting (Linux host, Windows guest), lobby/party UI, auto-connect, spawn of remote characters, chat,
map markers, launcher install/update flow.

## Findings from the 2026-10-27 test (fixed in b8341c3)

Float tick math froze interpolation; loading/dying while connected lost the server character; passengers sent
(0,0,0); items sent as empty names; player characters serialized without owner name/SteamID.

Still open: remote characters look like the *local* player. `[Appearance] 0 customization keys`: the 2.5 vtable
slots of gameuiCharacterCustomizationState (GetHead/Body/ArmsCustomization, `Game/Compat.h`) are unverified and
disabled, so no face/body keys are applied and the Muppet keeps the local V's look. Needs Cyberpunk2077.exe 2.5
(3.0.88.46235) disassembly to find the slots.

## Open problems (as of 0.2.0)

1. Remote characters barely move. Suspects: movement controller never attached in 2.5 (idle-controller hook hash
   2268141838, AttachController 4018412273, MultiMovementController vtable) or clock mismatch. Check `[Sync]` lines:
   controller yes/NO, "moves received", "clock ... difference".
2. Vehicles with a passenger: driver cannot drive / car teleports when the driver leaves. `DoMount` makes a car
   kinematic + not player-controlled when it thinks someone else drives it; check `[Vehicle] mount: ... remote-driven`
   on the driver's own car. Server: `[Sync] rejected move ... vehicle belongs to another player`.
3. Sandevistan crashes the client: get `[Crash]` lines + dump, and `[Script/Time]` lines just before.
4. "ItemID creation failed" for remote equipment: `[Appearance]` now names the item (mod only one side has?).

## In progress (0.2.2)

Mod Settings page "NS-CyberMP" (`World/ModOptions.reds` -> `Settings::Option`, saved in `mod/settings.json`),
nameplates + ping dot + pause info panel (`Rendering/Overlay.cpp`), party activity / shared waypoints / content-mod
comparison through Steam lobby member data (`Steam/SteamLobby.cpp`), follow-a-friend on the world map.

## Backlog after the sync bugs (agreed with Night, 2026-10-27)

1. Combat sync (aim/shoot/melee visuals, weapon in hand) - yes
2. Shared enemies (kills first, then NPC sync) - yes
3. Downed + revive - yes. If not revived in time, the downed player cannot respawn until the combat *near them* is over
   (not just "anyone on the map in combat").
4. Fast travel together (host travels -> guests get a prompt) - yes
5. Co-op gigs / NCPD jobs - maybe
6. Sandevistan rules - Night's design to workshop: the user moves at normal speed and everyone else slows down. If
   several players use one, the fastest Sandevistan feels normal, slower ones feel the difference, and players without
   one move as slowly as the fastest Sandevistan's scale.
7. Full appearance sync - yes, important: currently each player sees the other as *their own* character.
8. Vehicles fully (all seats, bikes, summoned car for everyone) - yes
9. Give items / eddies - yes
10. Weather sync - yes; police heat only when players share a vehicle
11. Cutscene/braindance handling: hide the other player during your scenes, a Mod Settings toggle, hidden by default
12. Proximity voice - no
13. Spectate a friend while dead/loading - yes
14. Rejoin last party - yes
