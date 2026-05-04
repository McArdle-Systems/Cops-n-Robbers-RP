# Phase 0a — AI Movement POC notes

## Status: POC complete

The original POC questions are answered, and we've shipped well past the
original scope. See [the dispatch audio task list](DISPATCH_AUDIO_TASKS.md)
for the explicit roadmap of what remains before "Living Town" Phase 0.

## Original POC scope (shipped)

Server-authoritative `RP_AIMovePOCComponent` attached to the GameMode
entity. On the first player spawn, it puts an infantry group + a crewed
vehicle ~25m from the player and exposes auto-follow to test "move to me."

Two paths exercised, both verified working on Arland:

1. **Infantry on foot:** `SCR_AIGroup` + `AIWaypoint_Move` placed at the
   player's position. Pathfinds correctly around the airfield.
2. **Vehicle:** `SCR_AIGroup` (crew) + `AIWaypoint_GetIn` to board, then
   `AIWaypoint_Move` to drive. Crew boards reliably with all-crew status
   detection; vehicle drives navmesh paths cleanly.

## Beyond the original POC — full dispatch system shipped

What started as "spawn AI and tell them to move to me" became a real
RP-game-mechanic prototype:

- **`RP_DispatchManagerComponent`** — controller on the GameMode with
  per-type group definitions (HMMWV, Police, EMS, etc.), per-type
  concurrent caps, named spawn-point references.
- **State machine** per dispatched unit: `SPAWNED → BOARDING_FOR_DISPATCH
  → DRIVING_TO_TARGET → DISMOUNTING → APPROACHING_ON_FOOT → LOITERING →
  BOARDING_TO_RETURN → RETURNING → IDLE_AT_SPAWN → DESPAWN`. Status-based
  triggers (actual mount/dismount detection) with timer fallbacks.
- **Re-dispatch handling** — units in `BOARDING_TO_RETURN` finish boarding
  before redirecting to a new target; units in `RETURNING` redirect
  immediately.
- **Cursor-active popup** — `RP_DispatchPopup : ChimeraMenuBase` opened
  by a custom rebindable input action (`RP_OpenDispatch`, default `J`).
  Same key both opens and closes. Doesn't fire in chat or map.
- **Per-unit ID logging** — each spawned unit gets a sequential ID
  (`Police#1`, `Police#2`) so multiple concurrent units of the same type
  are distinguishable in logs.

## Engine notes / risks (still relevant)

- **Drivers in town:** Civilian-context driving in Reforger is rough.
  Vehicles can get stuck on curbs, walls, or just give up. Observed
  during testing — one unit gave up, dismounted, and walked back. Future
  mitigation: stuck-vehicle detection.
- **Group ownership:** `SCR_AIGroup` lives only on the server. Component
  RPCs route through `Rpc(RpcAsk_*)` with `RplChannel.Reliable,
  RplRcver.Server`.
- **Waypoint cleanup:** Replacing waypoints requires explicitly removing
  all existing ones from the group; otherwise new ones queue behind.
- **Spawn ground placement:** `SCR_TerrainHelper.GetTerrainY` lifts to
  surface. Fine for now.
- **Navmesh requires actual terrain.** Empty worlds with prop "floors"
  silently fail. Arland's stock navmesh works once `SCR_AIWorld_Arland`
  is added to the world layer and tile cache is committed (~137MB).
  Captured in `memory/reforger_navmesh_terrain.md`.

## What this still doesn't prove

- Combat reaction (pure travel; no enemy placement)
- Civilian pedestrian behaviour (panic, idle wandering, etc.)
- Performance under load (>1 unit per type, many concurrent dispatches)
- Stuck-recovery behaviour (vehicle gives up → currently no automatic
  recovery; lower-priority follow-up)

## Promotion criteria — status

| Criterion | Status |
|-----------|--------|
| Infantry consistently pathfinds to the player | ✅ Verified on Arland airfield |
| Vehicle boards + drives to player | ✅ Verified end-to-end |
| ≥80% success on fixed 5-position test course | ⏳ Not formally measured |
| No script errors across 10 spawn/despawn cycles | ⏳ Not formally measured |
| Vehicle boards within 30s of spawn | ✅ Qualitatively verified (status-based triggers) |
| Confirmed working with Better Everon | ❌ Pivoted to Arland for nav/test simplicity |

The two ⏳ items are bookkeeping — the qualitative behaviour is solid,
just hasn't been formalised as a repeatable test course.

## Open questions — resolved

- ~~Default GUID for `AIWaypoint_Move.et` and `AIWaypoint_GetIn.et`~~ —
  resolved; both attributes filled on the manager components.
- ~~Best lightweight AI group prefab for the crew~~ — resolved.
  `Group_US_Transport.et` works for HMMWV testing; for production we
  custom-author groups inheriting from `Group_Base.et` with our character
  prefabs.
- `AIWaypoint_GetOut` for dismount — resolved (uses
  `SCR_BoardingWaypoint`, set vehicle position via `vehicle.GetOrigin()`).

## What's next (Phase 0)

- Audio acknowledgements for dispatch — see
  [DISPATCH_AUDIO_TASKS.md](DISPATCH_AUDIO_TASKS.md)
- Stuck-vehicle detection + recovery
- Formal test course measurement to tick off promotion criteria
- Civilian behaviour (pedestrians, panic) — separate sub-phase
