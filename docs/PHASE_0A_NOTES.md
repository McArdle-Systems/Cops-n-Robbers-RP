# Phase 0a — AI Movement POC notes

## What's in this POC

Server-authoritative `RP_AIMovePOCComponent` attached to the GameMode entity.
On the first player spawn, it puts an infantry group + a crewed vehicle ~80m
from the player and exposes DiagMenu actions to "move them to me."

Two paths exercised:

1. **Infantry on foot:** `SCR_AIGroup` + `AIWaypoint_Move` placed at the
   player's current world position. Tests baseline navmesh path-finding around
   buildings + props.
2. **Vehicle:** `SCR_AIGroup` (crew) + `AIWaypoint_GetIn` to board the vehicle
   on spawn, then `AIWaypoint_Move` for the move-to-me command. Tests the
   weakest part of Reforger's AI stack — civilian-context driving — without
   yet introducing a custom road graph.

## Known engine notes / risks

- **Drivers in town:** Civilian-context driving in Reforger is rough. The
  vehicle group may hit parked cars, low walls, or dead-end roads. We expect
  this; the POC's purpose is to characterise the failure modes, not to fix
  them. Capture videos of failures for the traffic-system design.
- **Group ownership:** `SCR_AIGroup` lives only on the server. RPCs from the
  local diag menu poll have to round-trip via `Rpc(RpcAsk_*)`. The component
  marks RPCs `RplChannel.Reliable, RplRcver.Server`.
- **Waypoint cleanup:** Replacing waypoints requires explicitly removing all
  existing waypoints from the group; otherwise new ones queue behind the old.
- **Spawn ground placement:** `SCR_TerrainHelper.GetTerrainY` lifts the spawn
  to the ground surface. If the player is on a building floor, this puts the
  AI on the ground below — fine for a POC.

## What this POC does NOT prove

- Combat reaction (pure travel only; no enemy placement)
- Multi-group coordination
- Dynamic spawn (only spawns once per session unless triggered via DiagMenu)
- Civilian behavior (pedestrians, panic, etc.) — future Phase 0a work
- Performance with N>1 group / vehicle (we only spawn one of each)

## Promotion to Phase 0

The bar to graduate (move from POC to "Living Town"):

1. ≥80% success on a fixed test course: spawn at a marked start position,
   issue Move-To-Me commands at 5 player positions across St. Pierre, log how
   many AIs reach the destination within 60s without operator intervention.
2. No script errors across 10 spawn/despawn cycles.
3. Vehicle group consistently boards the vehicle within 30s of spawn.

## Open questions to resolve in workbench

- Default GUID for `AIWaypoint_Move.et` and `AIWaypoint_GetIn.et` — populate
  in README once located in Resource Browser.
- Best lightweight AI group prefab for the crew (1-2 AI). Candidates from the
  base game's BLUFOR/OPFOR/USSR group sets.
