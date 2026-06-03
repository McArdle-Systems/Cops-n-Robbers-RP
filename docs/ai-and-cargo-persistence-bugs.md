# AI Standing Around + Floating Supplies — Bug Diagnosis & Fix Brief

## Status: FIXED in code (2026-06-03)

Both bugs traced to one root cause — traffic/dispatch entities leaking into the
engine savegame — and are fixed in:

- `Scripts/Game/Traffic/RP_TrafficLoopComponent.c`
- `Scripts/Game/Dispatch/RP_DispatchManagerComponent.c`

See **What Changed** at the bottom.

> **API correction:** an earlier draft of this brief recommended
> `EPF_Utils.SetPersistent(...)`. This mod does **not** use EPF — it uses the
> **native** Reforger persistence API, `PersistenceSystem.GetInstance().StopTracking(entity)`.
> `StopTracking` keeps an entity out of the save **and** purges any data already
> saved for it on the next save, and is a harmless no-op when no save system is
> running (workbench / save-less servers return null from `GetInstance()`).

---

## What Was Already Done (Known Working)

- `traffic-fixes.md` Fix 1 (exclude traffic vehicles from persistence) was applied
  via `StopTracking` on the **vehicle** and the **group container**
  (`RP_TrafficLoopComponent.SpawnVehicleAndCrew`).
- Evidence: after restart the traffic system spawns fresh from `CIV_Car_1`. ✓
- **Gap:** exclusion stopped at the vehicle + group container. It did **not** reach
  (a) the group's individual **character members**, nor (b) the vehicle's
  **supply-storage child entities**. Those two gaps are Bug 1 and Bug 2 below.

---

## Bug 1: AI Standing Around After Restart (+ pool won't refill)

### Observed Symptoms
1. After a restart, **20+ AI stand around** doing nothing — more than the 20-vehicle
   traffic budget. They are not in vehicles.
2. **The traffic pool won't refill after a reload** — even after dropping the AI
   target to 0 and back up to 20, no new traffic spawns.

### Root Cause
The group container was excluded from the save, but the **character entities inside
it were not.** Crew members:
- spawn **deferred** (not in the same frame as the group), and
- are **separate entities**, not children of the `SCR_AIGroup` container,

so `StopTracking(group)` never reached them. They persisted, then reloaded after a
restart with no group, no vehicle, and no orders → they stand forever.

Symptom 2 is the **same root cause**: those persisted standing characters (and any
phantom vehicles) sit **on the traffic spawn point.** `TrySpawnOne` →
`TryFindClearSpawnPos` → `IsSpotClear`/`ClearQueryCheck` treats any character or
vehicle in the clearance radius as a blocker and **defers the spawn**, logging
*"Spawn area … is blocked, deferring"* and eventually *"still blocked after N cycles
— pool stuck below target."* Toggling the target 0 → 20 does nothing because the
budget was never the blocker — the persisted crowd standing on the pad is.

> Both symptoms disappear once the crew members stop persisting.

### Dispatch contributes too
`RP_DispatchManagerComponent.SpawnUnit` spawned a cruiser + crew and excluded
**nothing** from persistence. After a restart those orphaned cops/cruisers also
stand around (the dispatch manager's unit list starts empty on a fresh start, so it
never reconciles them).

---

## Bug 2: Floating Supplies After Restart

### Observed Symptom
After restart, supply objects float at vehicle-deck height with no vehicle under them.

### Root Cause
The traffic/dispatch vehicle prefabs carry the **vanilla supply-storage feature** —
the cars spawn **with supplies**, which exist as **separate child entities** of the
vehicle. `StopTracking(vehicle)` does **not** cascade to children, so when Fix 1
excluded the car but not its supply children, the car was gone after restart but the
supplies reloaded at their last world-space position → floating at deck height.

(There is no custom cargo/crate system in this mod — earlier speculation about
"attached cargo crates" was wrong. It's vanilla vehicle supplies.)

### Fix
Make persistence exclusion **recursive over the entity's child hierarchy**, so
excluding the vehicle also excludes its supply-storage children. `StopTracking` on a
not-tracked child is a harmless no-op, so over-walking the whole subtree is safe.

---

## What Changed (implementation)

Both components now share the same exclusion shape:

```c
// Recursive: reaches supply-storage child entities under a vehicle.
void ExcludeFromPersistence(IEntity entity) {
    PersistenceSystem p = PersistenceSystem.GetInstance();
    if (!p) return;                 // workbench / save-less = no-op
    ExcludeSubtree(p, entity);
}
void ExcludeSubtree(PersistenceSystem p, IEntity e) {
    if (!e) return;
    p.StopTracking(e);
    IEntity c = e.GetChildren();
    while (c) { ExcludeSubtree(p, c); c = c.GetSibling(); }
}

// Crew members spawn deferred and aren't children of the group container.
void ExcludeCrewMembers(SCR_AIGroup crew) {            // signature == ScriptInvokerAIGroup
    PersistenceSystem p = PersistenceSystem.GetInstance();
    if (!p) return;
    array<AIAgent> agents = {};
    crew.GetAgents(agents);
    foreach (AIAgent a : agents) {
        IEntity ch = a.GetControlledEntity();
        if (ch) ExcludeSubtree(p, ch);
    }
}
```

Wired at each spawn site (traffic `SpawnVehicleAndCrew`, dispatch `SpawnUnit`):

```c
ExcludeFromPersistence(vehicleEnt);   // vehicle + supply children
ExcludeFromPersistence(crewEnt);      // group container
ExcludeCrewMembers(crew);             // members present this frame
crew.GetOnAllDelayedEntitySpawned().Insert(ExcludeCrewMembers);  // deferred members
```

`GetOnAllDelayedEntitySpawned()` is the native `SCR_AIGroup` invoker that fires once
all delayed-spawn members exist, passing the group — exactly the deferred case the
naive inline `GetAgents()` loop misses.

`StopTracking` also purges data **already** saved by earlier sessions on the next
save, so phantom standing AI and floating supplies left by pre-fix saves clean
themselves up after one save cycle.

---

## Summary Table

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| AI standing around + pool won't refill | Group container excluded, but its deferred, non-child character members were still persisted; they reload orphaned and also block the spawn point | Exclude each crew member via the group's `GetOnAllDelayedEntitySpawned` hook, in both Traffic and Dispatch |
| Floating supplies | Vehicles carry vanilla supply-storage; supplies are child entities and `StopTracking(vehicle)` doesn't cascade | Make exclusion recurse the vehicle's child hierarchy |
| Dispatch units orphaned after restart | `SpawnUnit` excluded nothing from persistence | Apply the same vehicle + group + member exclusion in Dispatch |

---

## Not Covered (potential follow-up)

- `RP_CopVehicleSpawnerComponent` (player-triggered cruiser dispenser) does not
  exclude its spawned vehicle from persistence. If those are meant to be ephemeral,
  the same recursive exclusion applies; if they're meant to persist, leave as-is.
  Out of scope for this pass.
