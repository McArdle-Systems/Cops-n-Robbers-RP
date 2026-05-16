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

1. **Spike: cycle resume after one-shot waypoint.** ✅ **Retired.**
   Replaced by snapshot/strip/restore — we don't depend on the cycle
   auto-resuming because we explicitly remove the waypoints, hold the
   references in a snapshot, and re-add them on release. Whether
   `AIWaypointCycle` preserves its internal position across remove +
   re-add of the same instance is a quality concern (worst case: the
   cycle restarts at waypoint 0, which is an acceptable visual
   artifact for civilian traffic) but no longer a correctness gate.
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
5. **Pull-over geometry.** ✅ **Done (in-lane).** Stop-in-lane via
   `RoadNetworkManager.GetReachableWaypointInRoad` with a goal 10m
   ahead in the vehicle's current heading. The "InRoad" snap
   guarantees a navmesh-reachable point on the road graph, so the AI
   doesn't give up and dismount the way it did with the off-road
   stub. Fallback path: if no road network is available (or the
   vehicle is off-road), Move waypoint goes at the vehicle's *current
   position* — completes immediately, empty queue holds the AI in
   place, no impossible navigation. Worst-case visual: vehicle stops
   exactly where it is rather than pulling forward.
   ⏳ **Side-of-road shoulder offset is follow-up polish** — current
   behavior is "stop in lane," not "pull off to the shoulder." Real
   shoulder requires `BaseRoad.GetWidth` + manual segment-direction
   from `BaseRoad.GetPoints` (BaseRoad doesn't expose a direct
   "direction at point" accessor) plus a navmesh check that the
   shoulder point is actually reachable.
6. **State machine driver.** ✅ **Done (snapshot/strip/restore +
   refresh-based release).** `m_mYieldedGroups` keyed by SCR_AIGroup,
   value `RP_YieldedGroupState` holding saved waypoints, the spawned
   Move waypoint, the assigned cop, the stopped vehicle, and a
   `m_fLastRefreshTime`. `BeginYield` snapshots, strips, spawns a
   Move, adds it, stamps the time. The bubble scan refreshes the
   timestamp on every tick the vehicle is still seen. `CheckReleases`
   runs at the top of every tick and releases any yield whose
   timestamp has gone stale beyond `m_fStaleReleaseSeconds` (default
   0.75s, ~3 ticks at the default 0.25s interval).
   Single-mechanism handling of all three real release cases:
   - Cop drives past → vehicle leaves bubble → no refresh → stale.
   - Cop kills lights → ScanBubble skipped → no refresh → stale.
   - Cop is deleted → no scan → no refresh → stale.
   Earlier distance-based release was wrong: the front bubble (100m)
   exceeded the release distance (60m), so any vehicle picked up
   beyond 60m forward immediately satisfied release, causing a
   per-tick Begin/End oscillation that yanked the AI's waypoints
   around enough that the driver dismounted.
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
   - **ACE Captives arrest during a yield.** ✅ **Handled.** When a cop
     downs the yielded driver and ACE pops them out of the vehicle for
     cuffing, `CheckReleases` would otherwise see the empty seat and
     treat it as a bail — prepending a GetIn waypoint that the resulting
     captive would then run to execute (cuffed AI sprinting back to its
     own car, generating ACE warnings). Fix is three checks plus a
     state-flag:
       1. `IsAnyGroupAgentUnconscious` — `CharacterControllerComponent.GetLifeState() != ALIVE`
          fires during the INCAPACITATED / DEAD window.
       2. `IsAnyGroupAgentCaptive` — `SCR_CharacterControllerComponent.ACE_Captives_IsCaptive()`
          (modded extension method, callable because ACE Captives is a
          hard dependency) fires once the captive wakes up cuffed.
       3. Either check sets `m_bArrestSilent` on the yield state and
          short-circuits the bail path. EndYield then drops the saved
          waypoint cycle entirely — without this last step, restoring
          the cycle re-introduces the same GetIn (the civilian patrol
          cycle already contains boarding waypoints), and the captive
          still runs back to the seat. With it, the group ends up with
          an empty queue and idles.
     `EvaluateCandidate` also early-returns for groups with a downed
     or cuffed member, so a new yield can't be initiated on a captive.

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
| AI in a traffic loop pulls over within 4s of entering the front bubble with cop lights on | ✅ Qualitatively verified |
| AI remains stopped while cop sits behind with lights on | ✅ Qualitatively verified |
| AI resumes within 6s of cop clearing (lights off OR cop > 60m away) | ✅ Qualitatively verified (refresh-based release, ~0.75s) |
| No script errors across 10 trigger/clear cycles | ✅ Qualitatively verified |
| Multiple loop vehicles in the bubble pull over independently | ✅ Qualitatively verified — multiple vehicles in the cone all stopped and resumed independently as expected |
| Lights off → never triggers | ✅ Qualitatively verified |
| Bail recovery: driver re-boards via prepended GetIn waypoint | ✅ Qualitatively verified once an off-road snap forced a bail |
| Re-yield after bail-recovery: driver re-yields naturally if still in bubble after re-boarding | ✅ Qualitatively verified |

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
