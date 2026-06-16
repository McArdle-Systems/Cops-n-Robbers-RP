# Cops n Robbers RP

Arma Reforger mission. RP-style police / civilian sandbox.

Design lives in [docs/CNR_2026_DESIGN.md](docs/CNR_2026_DESIGN.md)
(current scope split into Phase 1 / 2 / 3). Earlier phase notes:
[docs/old-design-docs/RP_MISSION_DESIGN.md](docs/old-design-docs/RP_MISSION_DESIGN.md),
[docs/old-design-docs/RP_DEPENDENCIES.md](docs/old-design-docs/RP_DEPENDENCIES.md).

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
- **Vehicle impound** — hold the Impound Sheet gadget, walk up to any
  impoundable vehicle, and the Impound action teleports it into a
  configured impound lot. Server-authoritative FIFO lot with slot
  recycling: a recovered car driven off its slot frees the slot
  without being destroyed, a wreck in a slot is cleared, and a full
  lot evicts (deletes) the oldest car. Faction-gated (`PD`).
  *(Shipped via PR #22.)*
- **In-game chat** — history panel + channels enabled via a world-
  placed `ScriptedChatEntity` (see `CHAT_HUD_INVESTIGATION.md`).

What's left in Phase 2:

- **Jail hold + automated release** at end of timer.

**Phase 3 — planned.** Manual plate/ID lookup interface as fallback
when LPR/radar don't trigger.

**World / mission infra (ongoing).** The mission moved off an Arland
SubScene onto a standalone **CustomWorld_Base** terrain (PRs #21, #23,
#24), with `SCR_AIWorld` relocated cnr-side and navmesh re-baked
against the new terrain. A custom **PlayableGroup** prefab + BLOB
formation and player-join / traffic-loop tweaks landed alongside
(PR #25). A lush forest-floor satmap + its Python generator landed in
PR #26.

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
- **`RP_ImpoundManagerComponent`** — server-authoritative impound lot
  on the GameMode. `Impound()` teleports a vehicle (via
  `Vehicle.Teleport`, so it replicates) into the first open slot, or
  FIFO-evicts the oldest car when the lot is full. Before each
  impound it reclaims slots whose car drifted past
  `m_fSlotOccupancyRadius` (frees the slot, leaves the car alone) or
  was wrecked in place (frees the slot, deletes the wreck).
- **`RP_ImpoundLotSlotComponent`** — marks a placed entity as one
  parking slot; its transform is the teleport target. Self-registers
  with the manager on every peer (so the action's client-side gate
  reads true on dedi). Markers are never deleted — re-read each
  impound so a moved marker is picked up.
- **`RP_ImpoundToolComponent`** + **`RP_ImpoundVehicleUserAction`** —
  the Impound Sheet gadget (marker component) and the vehicle action
  that requires it. Action is gated on faction (`m_sRequiredFactionKey`,
  default `PD`) and on the user holding the sheet; routes the request
  through `RP_PlayerRpcRelayComponent` so it lands server-side even
  when the cop isn't the vehicle's current controller. Add the action
  to the BAMC of every impoundable vehicle prefab.
- **`RP_TrafficLoopComponent`** — server-side civilian traffic pool on
  the GameMode. Maintains `m_iTargetActiveCount` AI vehicles cycling a
  shared waypoint loop, round-robining through `m_aSpawnConfigs`
  (group prefab + vehicle prefab pairs) and topping the pool back up
  on `m_fSpawnIntervalSeconds` when a vehicle dies or its group is
  wiped. World setup uses placed entities carrying
  `RP_TrafficMarkerComponent` (one `CREW_SPAWN`, two-plus indexed
  `WAYPOINT`s). The legacy `VEHICLE` marker type is deprecated.
- **`RP_EmergencyYieldComponent`** — server-side yield-to-emergency
  manager on the GameMode. For each cop vehicle with sirens on, runs a
  100 m-forward / 50 m-back bubble scan and pulls eligible AI civilian
  drivers over via the snapshot/strip/restore waypoint preemption
  pattern (empty queue = hold; restore on lights-off / cop-away /
  cop-deleted). Pull-over geometry is currently a stub offset; road-
  shoulder snapping is the open task in `YIELD_TO_EMERGENCY_TASKS.md`.
- **`RP_DriverComplianceComponent`** — tag + policy on the
  `SCR_AIGroup` prefab marking it a yield candidate. Groups without it
  are treated as not-applicable (players, dispatched units). Holds the
  server-only group registry the yield manager walks and the
  `WillComplyWithPullOver` decision (stubbed always-true for Phase 1;
  future flee / pursuit logic plugs in here).
- **`RP_VehicleHighlightComponent`** — manager on the GameMode that
  attaches a highlight-light prefab to scan targets for
  `m_fLifetimeSec`, refreshing the timer each tick the
  surveillance HUD re-detects the target so it stays lit while in
  cone. Snap-off (no fade) and depth-respecting (no through-wall
  render) by API limitation.
- **`RP_PlayerRpcRelayComponent`** — per-player client→server RPC
  bridge on the player character prefab (always client-owned, so RPCs
  route reliably — unlike GameMode-owned components on this dedi
  build). HUDs and actions use it to drive server-authoritative work
  (radar MFD power, impound request). Required on the cop character.
- **`RP_PlayerInventorySetupComponent`** — client-side quickslot pinner
  on a player character prefab. Polls until the owning client + the
  configured items are both ready, then pins each `m_aQuickSlotPins`
  entry (prefab-suffix match → slot index) — e.g. ACE Captives
  ZipCuffs into slot 9 on cop spawn. Beats the SCR client-side
  quickslot init pass that clobbers server-set entries on dedi.
- **`RP_PauseMenuInjector`** + **`RP_AdminPanelUI`** + **`RP_AdminUtils`**
  — admin traffic-cap panel. `modded PauseMenuUI` injects an entry
  button (admins only, cosmetic gate — the RPC re-checks server-side)
  next to Settings; the panel is a `ChimeraMenuBase` with a +/- slider
  for the live civilian-vehicle cap (default 6, hard max 50) that
  feeds `RP_TrafficLoopComponent`.

## Setup in Workbench

The test world `Worlds/TestWorld.ent` already has everything wired and
is built on the standalone **CustomWorld_Base** terrain, with
`SCR_AIWorld` carried cnr-side and navmesh committed to the repo.
Just open it and Press Play.

> **Terrain dependency:** `CustomWorld_Base` lives in a separate addon —
> [McArdle-Systems/CustomWorld](https://github.com/McArdle-Systems/CustomWorld).
> Clone it alongside this repo and enable it as a Workbench dependency.
> The [`cops-n-robbers-rp.code-workspace`](cops-n-robbers-rp.code-workspace)
> multi-root workspace expects it at `../CustomWorld`.

If recreating from scratch on a new world:

1. Open the world in Workbench (must be a real terrain world — empty
   worlds with mesh "floors" can't bake navmesh). Add an `SCR_AIWorld`
   to the layer if missing.
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
Scripts/Game/GameMode/          Player inventory / quickslot setup
Scripts/Game/Police/            Cop vehicle spawner (kiosk dispenser
                                with faction gate + occupancy check)
                                and the vehicle impound system
Scripts/Game/Surveillance/      Radar logic + visuals, watchlist,
                                surveillance HUD, vehicle highlight,
                                MFD helpers, RPC relay
Scripts/Game/Traffic/           Traffic loop, yield-to-emergency,
                                driver compliance
Scripts/Game/UI/Admin/          Pause-menu inject + admin traffic-cap
                                panel + admin utils
Prefabs/AI/Groups/              Custom AI group prefabs (police, civ)
Prefabs/Groups/                 PlayableGroup (custom playable group)
UI/                             Dispatch popup .layout
Missions/                       Mission scaffolding
Worlds/                         TestWorld (CustomWorld_Base terrain) +
                                committed navmesh tile cache
Tools/                          Blender/Python asset generators
                                (radar mesh, RT plane, speed textures,
                                lush satmap)
docs/                           Design notes, phase notes, audio
                                task list
```

## License

This work is licensed under the [Arma Public License Share Alike (APL-SA)](https://www.bohemia.net/community/licenses/arma-public-license-share-alike).
See [LICENSE.txt](LICENSE.txt). Use must be NonCommercial and ArmaOnly;
derivatives must be released under APL-SA. Attribution is required.
