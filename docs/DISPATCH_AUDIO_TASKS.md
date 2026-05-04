# Dispatch — Radio Chatter / Audio Tasks

Future-work checklist for adding voice acknowledgements to the dispatch
system. Captured here so we don't have to re-spec when we get back to it.

## Lines we want

- **"On the way"** — fired the moment a Dispatch request resolves to a
  unit (existing or freshly spawned).
- **"Unable"** — fired when Dispatch is called for a type that has no
  available unit AND is already at `m_iMaxSpawned`.
- **"On scene"** — fired on transition to `LOITERING` (unit has reached
  the player on foot and is loitering).
- **"Returning"** — fired on transition to `RETURNING` (unit is driving
  back to spawn point).

## Open decisions

- [ ] Source vs author: pull stock Reforger callsign lines, or record
  fresh clips? Stock is faster; fresh fits RP setting better.
- [ ] Playback path: 2D HUD-attached SFX (always heard regardless of
  distance to unit) vs 3D positional from the dispatched unit (more
  immersive but quieter when far away). Hybrid: dispatcher voice on
  request side (2D), unit voice on response side (3D).

## Implementation steps

- [ ] Land voice clips into the addon (e.g. `Assets/Audio/Dispatch/`).
- [ ] Add audio component to dispatched units (likely
  `SCR_AudioHandlerComponent` or similar) — wired to `RP_DispatchedUnit`
  lifecycle.
- [ ] Trigger **On the way** on successful `Dispatch()` (manager).
- [ ] Trigger **Unable** in the at-max-no-available branch of
  `DoDispatch`.
- [ ] Trigger **On scene** in `EnterState(LOITERING)`.
- [ ] Trigger **Returning** in `EnterState(RETURNING)`.

## Polish (later)

- [ ] Per-type voice variants — Police voice ≠ EMS voice ≠ HMMWV
  driver. Add a clip-set field per `RP_DispatchGroupDefinition`.
- [ ] Variation pool per line so it's not the same clip every time.
- [ ] Cooldown so spam-clicking the dispatch button doesn't talk over
  itself.
