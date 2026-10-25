# Port to Cyberpunk 2077 patch 2.5

Status: **compiles (syntax-checked), not yet tested in game.**

## What changed

| Area | Before | Now |
|---|---|---|
| RED4ext.SDK | Tilted Phoques fork, Nov 2024 (game 2.2x) | upstream `wopss/RED4ext.SDK` master (2.5 types), unmodified |
| RED4ext plugin API | v0 | v1 (`RED4ext::v1::*`); the plugin declares runtime 2.5 so RED4ext refuses to load it on other versions instead of crashing |
| ArchiveXL / TweakXL / Codeware | Dec 2024 | v1.28.0 / v1.12.0 / v1.21.0 (2.5 releases) |
| Script bundle layouts (`Game/ScriptValidation.h`) | 2.2x copy | current layouts from Codeware (`ScriptDefinition` grew 0x28) |
| CI | `windows-latest`, artifacts on push only | `windows-2022` (needs Windows SDK < 10.0.26100), manual runs, client DLL and Linux server uploaded as artifacts |

Everything the old SDK fork added that the client still needs now lives in **`code/client/Game/Compat.h`**, so the
SDK can be updated without re-applying patches.

## How fragile game internals were handled

Patches move struct fields and vtable slots. Where possible the port swapped hard-coded layouts for lookups that
survive patches:

- Item equip (`GiveItem`, `AddItemToSlot`): script-visible functions by name, arguments matched by parameter name
  (`Game/ScriptCall.h`) instead of `gameITransactionSystem` vtable slots (which shifted by 8 bytes in 2.5).
- Item appearance name: TweakDB flat `Items.X.appearanceName` instead of a hard-coded `Item_Record` layout.
- Puppet move component: found among the entity's components instead of a fixed `gamePuppet` offset.
- Vehicle transform/speed: placed component transform and `GetCurrentSpeed()` instead of the physics rigid body.
- Engine start, gender, chat UI event: script-visible `TurnEngineOn`, `IsBodyGenderMale`, `QueueEvent`.

Tags in `Compat.h`: `[robust]` (by name/type), `[same]` (neighbouring fields did not move in 2.5),
`[likely]` (block moved as a whole), `[VERIFY]` (2.2x value, may be wrong on 2.5).

## Disabled until verified

`[VERIFY]` items are compiled out unless `CPMP_UNVERIFIED_OFFSETS` is defined, because a wrong write corrupts memory:

- `CCState_Get{Head,Body,Arms}Customization` vtable slots: remote players get clothes but **not** their face/body
  morphs yet.
- `WheeledVehicle_EngineData` (engine flag `unk61` on mounting).

## To check in game

1. RED4ext log loads `CyberpunkMP` with no address errors. A missing address hash shows a RED4ext message box naming
   the hash; 3 of the client's 35 hashes are confirmed by other 2.5 mods, the rest need the game's
   `bin/x64/cyberpunk2077_addresses.json` (or a run) to confirm.
2. redscript compiles `assets/redscript` (Codeware API changes between 1.13 and 1.21 would show here).
3. Two clients see each other move (puppet sync, `[same]` move-representation offset).
4. Remote player has the right clothes (`GiveItem`/`AddItemToSlot` by name).
5. Vehicles: enter, drive, passenger sync (`physicsState` bit 0x100, `ForceMoveTo`).
6. Chat input caret (`[likely]` caret offset).
