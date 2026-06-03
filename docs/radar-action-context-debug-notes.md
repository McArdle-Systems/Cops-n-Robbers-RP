# Radar Action Context Debug Notes

Last updated: 2026-05-31

## Goal

Make the speed radar actions on `RP_Suburban_police.et` usable from the driver seat through the radar-specific action contexts, not by piggybacking on an existing vanilla context like `starter_switch`.

## Current Symptom

- Radar actions can be discovered while outside the vehicle and during the get-in transition.
- Once fully seated, normal interaction discovery stops finding the custom radar contexts.
- Holding Alt / "find interaction points" can still reveal the radar interaction points and causes action debug logs to start printing.
- This means the contexts/actions exist and can be evaluated, but the normal seated interaction collector is not selecting them.

## Current Context Setup

Radar actions currently live on the police Suburban vehicle `ActionsManagerComponent`, not on the slotted radar prop.

Relevant prefab:

- `Prefabs/Vehicles/Wheeled/Suburban/RP_Suburban_police.et`

Contexts:

- `Toggles`
  - Currently changed to `RP_RadarUserActionContext`.
  - Contains `Power` and `Toggle Plate Reader`.
- `DistanceKnob`
  - Contains `Distance +` and `Distance -`.
- `TargetSpeedKnob`
  - Contains `Target Speed +` and `Target Speed -`.

Current actions force active context via `m_sForcedContextName`:

- `Power` -> `Toggles`
- `Toggle Plate Reader` -> `Toggles`
- `Distance +` -> `DistanceKnob`
- `Distance -` -> `DistanceKnob`
- `Target Speed +` -> `TargetSpeedKnob`
- `Target Speed -` -> `TargetSpeedKnob`

`Power` may still have `starter_switch` in its `ParentContextList` as a known-good comparison/fallback from testing. The desired final state is radar actions on radar contexts, not on `starter_switch`.

## Script State

Relevant files:

- `Scripts/Game/Surveillance/RP_RadarUserActionBase.c`
- `Scripts/Game/Surveillance/RP_RadarUserActionContext.c`
- `Scripts/Game/Surveillance/RP_RadarTogglePowerUserAction.c`
- `Scripts/Game/Surveillance/RP_RadarToggleLPRUserAction.c`
- `Scripts/Game/Surveillance/RP_RadarAdjustDistanceUserAction.c`
- `Scripts/Game/Surveillance/RP_RadarAdjustTargetSpeedUserAction.c`

`RP_RadarUserActionBase` currently:

- Extends `SCR_VehicleActionBase`.
- Sets:
  - `m_bInteriorOnly = true`
  - `m_bPilotOnly = false`
  - `m_bIsToggle = false`
- Does not call `super.CanBeShownScript(user)` or `super.CanBePerformedScript(user)`.
  - Earlier tests with `super` made labels disappear everywhere.
  - Because `super` is bypassed, `m_bInteriorOnly` is not the effective gate for our script visibility.
- Uses `m_sForcedContextName` to call `SetActiveContext()` with the named context from `ActionsManagerComponent`.
- Logs:
  - context indexes on init
  - forced context debug description
  - show checks
  - throttled perform checks

`RP_RadarUserActionContext` currently:

- Extends `UserActionContext`.
- Adds a `GetRadarDebugDescription()` helper.
- Does not override discovery behavior. The available `UserActionContext` API appears mostly engine-owned/read-only.

## Confirmed Facts From Logs

- The action scripts are not deciding to hide the actions.
- While the contexts are discovered, actions return:
  - `shown=1`
  - `canPerform=1`
- Forced active contexts now work; the previous `activeContext='<none>'` noise was cleaned up.
- Recent logs show examples like:
  - `ForcedContext ... name='Toggles' radius=2 origin=<649.684,1.8845,535.224> actions=2`
  - `activeContext='Toggles' shown=1`
  - `activeContext='DistanceKnob' shown=1`
  - `activeContext='TargetSpeedKnob' shown=1`
- Full seated logs from earlier had the character around `y ~= 0.96`.
- The logs immediately before stopping preview often only capture get-in transition coordinates, such as `y ~= 0.12-0.16`.
- `Workbench Reload Game` at the end of a log means the preview session was stopped and Workbench returned to the editor.

## Debugging Conclusions

The remaining issue is probably not:

- faction checks
- action names
- `CanBeShownScript()`
- `CanBePerformedScript()`
- missing action UI info
- missing action parent context
- bad active context
- radar prop vs vehicle action ownership
- basic point placement

The likely issue is:

- the normal seated vehicle/interior interaction collector switches to a different discovery path after entering the vehicle, and that path does not include the custom radar contexts.

Alt / "find interaction points" likely uses a broader or forced discovery mode, which is why it can still see the contexts.

## Tests Already Tried

- Actions on slotted `RP_SpeedRadar.et`:
  - initialized, but were not queried by normal player interaction in this vehicle setup.
- Actions on vehicle `ActionsManagerComponent`:
  - discovered outside and during get-in transition.
- `SCR_ScriptedUserAction` / `ScriptedUserAction` / `SCR_VehicleActionBase` experiments:
  - current base is `SCR_VehicleActionBase`.
- Calling `super.CanBeShownScript()` / `super.CanBePerformedScript()`:
  - made labels disappear everywhere in testing.
- Adding context UI info:
  - did not solve seated discovery.
- Toggling `Display UI at reference Point`:
  - placed UI strangely, did not solve seated discovery.
- Toggling predicate cache:
  - no confirmed fix.
- Moving action points closer / near starter switch:
  - did not solve seated normal discovery.
- Adding `RP_RadarUserActionContext`:
  - compiled and logs runtime context details, but does not solve discovery by itself.

## Current Hypothesis

The vehicle has an inherited working `starter_switch` context. A radar action attached to `starter_switch` worked inside the vehicle. Custom radar contexts do not, even when placed near known working points.

This suggests `starter_switch` is part of a vanilla seated/interior action source that custom child-prefab contexts are not automatically entering, or the seated collector has additional filtering not visible from the action scripts.

## Useful Next Steps

1. Find where `starter_switch` is defined in inherited vanilla resources, if possible.
2. Compare any hidden context/component metadata around `starter_switch` against custom contexts.
3. Look for vehicle-interior interaction collector/cache behavior in public API/docs or extracted scripts.
4. Keep the forced-context cleanup unless it causes problems; it removes ambiguity.
5. Consider removing or reducing debug spam once the issue is solved.

## Related Side Quest

Single-person unflip tuning was adjusted successfully on the Suburban:

- `m_fMassToForce 8`
- `m_fForceLimit 12000`
- `m_fLinearSpeedLimit 1.5`
- `m_fAngularSpeedLimit 1.5`
- `m_fSuggestedUsersMultiplier 0`

This avoided the previous high-force "yeet into space" behavior.
