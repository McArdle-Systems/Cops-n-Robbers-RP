# Cops n Robbers RP

Arma Reforger mission. RP-style police / civilian sandbox.

Design lives in `~/.openclaw/workspace-main/RP_MISSION_DESIGN.md` and `RP_DEPENDENCIES.md`.

## Status

**Phase 0a (AI movement POC) is complete.** Infantry pathfinds, vehicle
crews board and drive end-to-end on Arland. Past the original POC scope
we shipped a real dispatch system: rebindable cursor-active popup, on-
demand spawning by type, full state machine for the dispatch loop.

See [docs/PHASE_0A_NOTES.md](docs/PHASE_0A_NOTES.md) for what shipped
and what's next on the road to Phase 0 ("Living Town").

## What's in here

- **`RP_DispatchManagerComponent`** — dispatch controller on the
  GameMode entity. Holds per-type group definitions (HMMWV, Police,
  EMS, etc.) with concurrent caps, named spawn-point references, and
  drives the per-unit state machine.
- **`RP_DispatchSpawnPointComponent`** — marks a placed entity in the
  world as a named spawn/return point. Defaults to using the entity's
  Workbench name as the lookup key.
- **`RP_DispatchHUDComponent`** — opens the cursor-active dispatch
  popup on a custom rebindable input action (`RP_OpenDispatch`,
  default `J`). Pressing the same key again or `ESC` closes the popup.
- **`RP_AIMovePOCComponent`** — original Phase 0a POC component. Auto-
  follows the player on a configurable timer for movement validation
  testing. Standalone from the dispatch system; useful for navmesh
  smoke tests in new worlds.

## Setup in Workbench

The test world `Worlds/TestWorld.ent` already has everything wired and
is built as a SubScene of Arland with navmesh committed to the repo.
Just open it and Press Play.

If recreating from scratch on a new world:

1. Open the world in Workbench (must be a real terrain world — Arland
   recommended). Add `SCR_AIWorld_Arland` to the layer if missing.
2. Generate navmesh via the Workbench navmesh tool. (See
   [`memory/reforger_navmesh_terrain.md`](../../.claude/projects/cops-n-robbers-rp/memory/reforger_navmesh_terrain.md)
   in our project memory for the gotcha.)
3. Add a `GenericEntity` at each spawn location for each dispatchable
   type. Attach `RP_DispatchSpawnPointComponent` to it. Either rename
   the entity to your spawn-point key (e.g. `SpawnPoint_Police`) or
   set the `Name` override on the component.
4. On the GameMode entity, attach `RP_DispatchManagerComponent`:
   - Fill `Move Waypoint Prefab` → `Prefabs/AI/Waypoints/AIWaypoint_Move.et`
   - Fill `GetIn Waypoint Prefab` → `Prefabs/AI/Waypoints/AIWaypoint_GetIn.et`
   - Fill `GetOut Waypoint Prefab` → `Prefabs/AI/Waypoints/AIWaypoint_GetOut.et`
   - Populate the `Definitions` array — one entry per dispatchable type
     (Type Tag, Crew Group Prefab, Vehicle Prefab, Spawn Point Name,
     Max Spawned, etc.)
5. On the GameMode entity, also attach `RP_DispatchHUDComponent`. Set
   `Dispatch Type` to a tag from your definitions array.
6. Press Play. Press `J` (or whatever `RP_OpenDispatch` is bound to in
   Settings → Controls → Cops n Robbers RP) to open the popup. Click
   the dispatch button. AI spawns at the spawn point and drives to you.

## Layout

```
addon.gproj                     Project definition + ArmaReforger dependency
Configs/System/                 chimeraInputCommon, chimeraMenus,
                                keyBindingMenu — input action +
                                custom-menu registration
Scripts/Game/AI/                Phase 0a POC component (auto-follow)
Scripts/Game/Dispatch/          Dispatch system (manager, units, HUD,
                                spawn point component, popup menu)
Prefabs/AI/Groups/              Custom group prefabs (police, etc.)
UI/                             Dispatch popup .layout
Missions/                       Mission scaffolding
Worlds/                         TestWorld (SubScene of Arland) +
                                committed navmesh tile cache
docs/                           Design notes, phase notes, audio
                                task list
```

## License

This work is licensed under the [Arma Public License Share Alike (APL-SA)](https://www.bohemia.net/community/licenses/arma-public-license-share-alike).
See [LICENSE.txt](LICENSE.txt). Use must be NonCommercial and ArmaOnly;
derivatives must be released under APL-SA. Attribution is required.
