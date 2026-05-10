# Yield to Emergency Vehicle — Phase 1 Plan

When an emergency vehicle's lights are on, AI drivers in a forward bubble
pull off the road and stop. They stay stopped while the cop sits behind
them with lights on, and resume once the cop has cleared (lights off or
moved away). Generic across any vehicle marked as emergency, not specific
to police.

## In scope

- Detect lights state on emergency vehicles (gated by
  [RP_PoliceVehicleComponent](Scripts/Game/Surveillance/RP_SurveillanceHUDComponent.c)
  — rename / split if non-police vehicles get sirens later).
- Server-side bubble scan around each emergency vehicle.
- Per-target pull-over: snap to road-shoulder via `RoadNetworkManager`,
  splice in a Move waypoint, AI brakes and parks.
- Resume: when the "cleared" condition holds, remove pull-over and the
  AI's cycle (or other waypoints) continues.
- Compliance hook on the driver/group, stubbed always-comply.

## Out of scope (this phase)

- Real flee / refusal logic (compliance hook returns true).
- Pedestrian yielding.
- Cross-traffic / oncoming-lane yielding.
- Hand-placed pull-over markers — relying on road-network geometry.
- Plate registry, arrest, ticket UI — separate features.

## Architecture

New: `Scripts/Game/Traffic/RP_EmergencyYieldComponent.c`

- `RP_EmergencyYieldComponent : SCR_BaseGameModeComponent`
  Server-only manager on the GameMode. Ticks at ~4Hz. Iterates every
  registered emergency vehicle, runs the bubble scan, drives the
  per-target state machine.
- `RP_DriverComplianceComponent : ScriptComponent`
  Lives on the **SCR_AIGroup prefab**. Exposes
  `bool WillComplyWithPullOver()` — stub returns `true`. Hook for
  future flee logic. Two scopes that look like one but aren't:
  *compliance decision* is per-group (one yield/flee answer per
  convoy), *target selection* is per-driver (the manager identifies
  which driver's vehicle is in the bubble and the group routes the
  pull-over waypoint to just that driver). Multi-vehicle convoys
  still yield only the affected driver.

Touch:

- [RP_PoliceVehicleComponent](Scripts/Game/Surveillance/RP_SurveillanceHUDComponent.c) —
  static instance registry; `OnPostInit` adds, `OnDelete` removes.
  Manager iterates this set.

Per-target state machine, keyed by AI group:

```
DRIVING → PULLING_OVER → STOPPED → RESUMING → (back to DRIVING)
```

State transitions:

- DRIVING → PULLING_OVER: target enters bubble, lights on, compliance
  returns true. Compute shoulder point, splice in Move waypoint at low
  speed.
- PULLING_OVER → STOPPED: vehicle within ~5m of target waypoint AND
  speed < ~2 km/h.
- STOPPED → RESUMING: lights off OR `distance(cop, stoppedCar) > 60m`.
  (Cone exit alone is wrong — pulling alongside leaves the cone but
  shouldn't release.)
- RESUMING → DRIVING: pull-over waypoint removed, normal waypoints
  take over (per spike outcome below).

## Implementation steps

1. **Spike: cycle resume after one-shot waypoint.** Place a Move
   waypoint *before* an active `AIWaypointCycle` on a loop vehicle.
   Verify whether the agent returns to the cycle automatically when
   the Move waypoint is satisfied, or whether the cycle has to be
   re-added. Outcome drives step 6. ⏳ Not yet run.
2. **Spike: server-side `GetLightsState`.** ✅ **Verified.**
   `BaseLightManagerComponent.GetLightsState(ELightType.Dashboard)`
   reads the toggled state correctly server-side, so no RPC wrapper
   needed. Confirmed by `[RP_Yield] Lights ON/OFF` transition logs
   firing in sync with the in-vehicle dashboard action.
3. **Emergency-vehicle registry.** ✅ **Done.** Static instance set
   on `RP_PoliceVehicleComponent`, server-only registration via
   `OnPostInit` / `OnDelete`.
4. **Bubble scan.** ✅ **Done.** 100m front / 50m back, sphere query
   + dot-product partition. `RP_EmergencyYieldComponent.ScanBubble`.
5. **Pull-over geometry.** ⏳ Not yet implemented.
   - `RoadNetworkManager.GetClosestRoad(targetPos, out road, out d)`.
   - Use the road's direction at that point; cross with world up
     gives the shoulder normal.
   - `targetPos + shoulderNormal * 3.5m`.
   - `RoadNetworkManager.GetReachableWaypointInRoad(agentPos, shoulderPoint, range, out wp)`
     to snap to a navmesh-valid point.
   - Spawn `AIWaypoint_Move` at the result with a low speed override.
6. **State machine driver.** ⏳ Not yet implemented. Server-side
   dictionary keyed by AI group. Tracks: cop ref, target waypoint,
   state, entry time. Manager tick advances state per the rules above
   and cleans up entries when the group or cop is gone. Pull-over
   waypoint removal at RESUMING uses the path determined by the spike
   (re-add cycle vs trust auto-fall-back).
7. **Compliance hook.** ✅ **Done (stub).** Lookup uses a registry of
   `RP_DriverComplianceComponent` + `SCR_AIGroup.GetAgents()` membership
   check. `AIAgent.GetParentGroup()` returns null for an active driver
   in our setup, so we don't rely on it; instead, for each registered
   compliance group we walk its agent list and match each agent's
   `GetControlledEntity()` against the seated driver entity. The driver
   is found by walking the vehicle's *children* (skipping the vehicle
   itself, since AI-driven vehicles carry their own AIControlComponent
   that would short-circuit the search).
8. **Edge cases.**
   - Multiple cops bubble-overlapping the same target: first one
     claims it; others ignore until cleared.
   - Cop deleted mid-stop: release all that cop's claims, RESUMING
     fires for each.
   - AI vehicle deleted mid-stop: drop the dictionary entry, no
     waypoint cleanup needed.
   - Lights flick off → on rapidly: claim is per-cop, so the existing
     STOPPED claim survives the flicker.

## Engine notes / risks

- AI driving in towns is rough per
  [PHASE_0A_NOTES.md:48-50](docs/PHASE_0A_NOTES.md#L48-L50). Pull-over
  success rate will be uneven on tight roads. Acceptance is "stops
  *somewhere* off-lane below a speed threshold," not "perfectly parked
  on the shoulder."
- `AIWaypointCycle` resume after a one-shot insert is the main unknown
  — see spike step 1. Plan B is snapshot-and-rebuild.
- `GetReachableWaypointInRoad` may return a point still in-lane if the
  shoulder offset is too small. Tunable; default 3.5m.
- `BaseLightManagerComponent` replication for server-side reads is the
  other unknown — spike step 2 confirms.
- Sphere queries are O(n) per cop; with 4Hz tick and a handful of cop
  vehicles this is a non-issue. Worth re-measuring if cop count grows.

## Promotion criteria

| Criterion | Status |
|-----------|--------|
| AI in a traffic loop pulls over within 4s of entering the front bubble with cop lights on | ⏳ |
| AI remains stopped while cop sits behind with lights on | ⏳ |
| AI resumes within 6s of cop clearing (lights off OR cop > 60m away) | ⏳ |
| No script errors across 10 trigger/clear cycles | ⏳ |
| Multiple loop vehicles in the bubble pull over independently | ⏳ |
| Lights off → never triggers | ⏳ |

## Follow-ups (later phases)

- Real compliance logic — flee branch, pursuit kickoff, "this driver
  is wanted" wiring through plate registry.
- Cross-traffic / oncoming yielding.
- Pedestrian reactions.
- Hand-placed pull-over hint markers in problem spots where the road
  query gives bad results.
- Hazards-on visual polish while stopped.
- Radio chatter when a stop is initiated ("traffic stop in progress").
- **Passenger handling in stopped vehicles.** Assuming Reforger AI
  follows "no override = continue prior behavior," passengers stay
  seated by default in any of the future scenarios (driver dismounts,
  driver flees on foot, vehicle just sits) without needing per-member
  waypoints. Becomes an explicit design question only for flagged
  stops ("everyone out") where the group needs a dismount order, or
  if the engine assumption above turns out not to hold and we have to
  pin passengers manually. Same assumption underwrites the
  AIWaypointCycle resume-after-one-shot question — if it holds for
  one, it holds for both.
