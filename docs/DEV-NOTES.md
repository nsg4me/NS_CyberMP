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

## Open problems (as of 0.2.0)

1. Remote characters barely move. Suspects: movement controller never attached in 2.5 (idle-controller hook hash
   2268141838, AttachController 4018412273, MultiMovementController vtable) or clock mismatch. Check `[Sync]` lines:
   controller yes/NO, "moves received", "clock ... difference".
2. Vehicles with a passenger: driver cannot drive / car teleports when the driver leaves. `DoMount` makes a car
   kinematic + not player-controlled when it thinks someone else drives it; check `[Vehicle] mount: ... remote-driven`
   on the driver's own car. Server: `[Sync] rejected move ... vehicle belongs to another player`.
3. Sandevistan crashes the client: get `[Crash]` lines + dump, and `[Script/Time]` lines just before.
4. "ItemID creation failed" for remote equipment: `[Appearance]` now names the item (mod only one side has?).

## Next ideas

- In-game "NS-CyberMP" settings tab (debug mode toggle first; natives `IsDebugMode`/`SetDebugMode` exist).
