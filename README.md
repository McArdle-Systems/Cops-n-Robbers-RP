# Cops n Robbers RP

Arma Reforger mission. RP-style police / civilian sandbox.

Design lives in [docs/CNR_2026_DESIGN.md](docs/CNR_2026_DESIGN.md)
(current scope split into Phase 1 / 2 / 3). Earlier phase notes:
`~/.openclaw/workspace-main/RP_MISSION_DESIGN.md`, `RP_DEPENDENCIES.md`.

## Status

**Phase 1 shipped 2026-05-16** — merged to `main` via PRs #6 and #11.
What's in:

- **Radar / speed detection** — vehicle-mounted speed radar with AG0
  MFD screen (live page when HUD open, blank page when closed), peak/
  lock flow, replicated speed signal.
- **Yield-to-emergency** — civilian AI yields to cop cars with sirens
  on. Snapshot/strip/restore waypoint preemption; ACE Captives arrest
  path silent-releases without re-running the patrol cycle.
- **Dispatch audio** through a physical radio prop (3D positional, not
  2D HUD).
- **Cop loadout polish** — ACE Captives ZipCuffs auto-wired into
  quickslot 9 on cop spawn.
- **Traffic-cap admin panel** injected into the pause menu.
- Plus the dispatch system, custom inputs, and cop-car dispenser
  carried over from Phase 0a.

**Phase 2 — in progress.** What's in:

- **LPR / watchlist match** — `RP_PlateWatchlistComponent` on the
  GameMode (server-only) holds flagged plates and auto-budgets to a
  configurable percent of active traffic (default 5%, floor 1).
  Radar `IsPlateFlagged()` consults it; SCANNING -> FLASHING triggers
  on speed OR plate match, with per-lock fired flags so a plate-only
  lock picks up a delayed speed beep if the target later starts
  speeding. HUD adds independent `SPEEDING` and `WATCHLIST` badges
  so you can tell overspeed / LPR hit / both apart at a glance. LPR
  hit also pops a hint popup with the plate for the cop driver only.
  *(Shipped 2026-05-18 via PR #19, branch `feature/lpr-watchlist`.)*

What's left in Phase 2:

- **Jail hold + automated release** at end of timer.

**Phase 3 — planned.** Manual plate/ID lookup interface as fallback
when LPR/radar don't trigger.

Earlier phase status: see [docs/PHASE_0A_NOTES.md](docs/PHASE_0A_NOTES.md)
for the AI-movement POC that came before Phase 1.

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
- **`RP_SpeedRadarLogicComponent`** — server-authoritative radar on
  the cop vehicle. Cone scan (default 22.5° half-angle, 50 m) picks
  the closest target each tick; SCANNING -> FLASHING transition on
  speed overshoot OR plate watchlist hit; per-lock fired flags so
  each alert sound fires at most once per lock cycle. Speed display
  is peak-tracking for speed-locks and live-instant for plate-only
  locks. Broadcasts a per-tick snapshot (state + speed + plate +
  lockReason bitmask); HUD and visual component read it.
- **`RP_SurveillanceHUDComponent`** — toggleable cop-driver HUD
  overlay (default key `]`, action `RP_ToggleSurveillance`). Reads
  the radar's snapshot, renders the SPEED / PLATE fields and the
  independent `SPEEDING` / `WATCHLIST` badges, and fires the LPR
  hit popup on watch-bit raised. Display-only; the server drives
  everything.
- **`RP_PlateWatchlistComponent`** — server-only watchlist on the
  GameMode. Storage + `IsWatched` / `AddWatch` / `RemoveWatch` /
  `MatchPartial` / `MaintainBudget`. `m_fBudgetPercent` (default
  0.05) and `m_iMinBudget` (default 1) tune the auto-budget;
  `m_aInitialWatchPlates` hand-pins specific plates. Traffic loop
  calls `MaintainBudget` after any pool change (admin cap change,
  spawn, prune).
- **`RP_CopVehicleSpawnerComponent`** + **`RP_SpawnCopVehicleUserAction`**
  — interactable cop-car dispenser. Place the component (plus an
  `ActionsManagerComponent` containing the user action, plus an
  `RplComponent`) on any kiosk-like entity. Configure:
    - `m_aVehiclePrefabs` — array of dispensable vehicle prefabs
      (currently dispenses entry [0]; array shape leaves room for a
      future selection UI).
    - `m_sSpawnPointEntityName` — name of a separate placed entity
      whose transform is used as the spawn pad (position + rotation).
      Falls back to the component's owner transform + a local offset
      if empty / not found.
    - `m_aAllowedFactionKeys` — faction-key gate (empty = anyone).
      Backed by `FactionAffiliationComponent.GetAffiliatedFactionKey`
      on the activating user.
    - `m_fOccupancyRadius` — sphere check at the spawn pad refuses
      dispatch if any `Vehicle` is inside. Prevents stacking cars.
  The action's `CanBePerformedScript` surfaces refusal reasons
  (`"Police only."`, `"Spawn pad is occupied."`) to the prompt UI so
  the player sees the gate, not a silent no-op.

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
Scripts/Game/Police/            Cop vehicle spawner (kiosk-style
                                dispenser with faction gate +
                                occupancy check)
Scripts/Game/Surveillance/      Radar logic + visuals, watchlist,
                                surveillance HUD, MFD helpers, RPC
                                relay
Scripts/Game/Traffic/           Traffic loop, plate registry, yield-
                                to-emergency, driver compliance
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
