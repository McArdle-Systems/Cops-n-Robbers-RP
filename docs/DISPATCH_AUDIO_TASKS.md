# Dispatch — Radio Chatter / Audio Tasks

Living doc for dispatch radio chatter. The four core lines are wired and
audible end-to-end; this file tracks what's done plus the polish backlog.

## Lines (all wired)

- **"On the way"** — fires when `Dispatch()` resolves to a unit (existing
  or freshly spawned). For fresh spawns the trigger is deferred ~1500ms
  via `CallLater(DeferredOnWaySound)` so AI agents and inventory items
  have time to materialize before the radio lookup runs.
- **"Unable"** — fires in the at-max-no-available branch of `DoDispatch`.
  Each client plays it on the radio in their own local player's inventory.
- **"On scene"** — fires from `EnterState(LOITERING)`.
- **"Returning"** — fires from `EnterState(RETURNING)`.

## Asset / wiring summary

- WAVs in [Assets/Audio/Dispatch/](Assets/Audio/Dispatch/): `OnTheWay.wav`,
  `Unable.wav`, `OnScene.wav`, `RTB.wav`. Mono headset recordings.
- SoundEvents authored in [DispatchAudio.acp](Assets/Audio/Dispatch/DispatchAudio.acp):
  `SOUND_DISPATCH_ONWAY`, `SOUND_DISPATCH_UNABLE`,
  `SOUND_DISPATCH_ONSCENE`, `SOUND_DISPATCH_RTB`. Each event is configured
  3D / spatialized in the audio editor (sets attenuation curve so audio
  falls off with distance).
- Audio component on the radio prop, not on the cop character.
  [RP_Radio_ANPRC68.et](Prefabs/Items/Equipment/Radios/RP_Radio_ANPRC68.et)
  has a `SoundComponent` referencing `DispatchAudio.acp`. We tried two
  others first: `SCR_CommunicationSoundComponent` (silently no-ops for
  non-character entities — it's tied to character voice / VON), and
  `RadioBroadcastSoundComponent` (broadcasts everywhere ignoring
  position, no falloff). Plain `SoundComponent` plus a 3D-configured
  SoundEvent gives proper positional audio.
- Script trigger pattern: helpers in
  [RP_DispatchManagerComponent.c](Scripts/Game/Dispatch/RP_DispatchManagerComponent.c)
  use a broadcast `RplRpc` so each client plays the SoundEvent locally —
  server-side `SoundEvent` calls don't auto-replicate. Audio is played
  via `SoundEventTransform(name, transf[])` with the **carrier's
  transform**, because stowed inventory items often have a world
  transform of (0,0,0) — pinning playback to the carrier guarantees the
  audio emanates from where the cop actually is.
- **Duplex playback** (verified): each broadcast RPC handler plays the
  SoundEvent *twice* on the local machine — once on the dispatched
  cop's radio (positional 3D from the cop's location, fades with
  distance) and once on the local player's own radio (positional 3D
  from the player's pocket, always close). Models real handheld-radio
  behaviour: nearby cops are heard directly + via your radio relay;
  distant cops are only audible via your radio. Inventory lookup uses
  `FindItemWithComponents([BaseRadioComponent], PURPOSE_ANY)` —
  default `PURPOSE_DEPOSIT` misses items in gadget slots.
- **Popup gate**: `RP_DispatchHUDComponent.OpenPopup` checks the local
  player's inventory for a `BaseRadioComponent`-bearing item via the
  same `LocalPlayerHasRadio()` helper. No radio → popup blocked, big
  WARNING in log. Net result: you can't even open the dispatch HUD
  without a radio in your kit.

## Polish (later)

- [ ] **Earpiece support.** When implemented, audio routing will branch
  per listener: with earpiece equipped → 2D-only for the wearer (private,
  others can't hear); without earpiece → current positional 3D from radio
  speaker. The duplex model is groundwork for this; the local-player
  playback is what would become 2D-private when an earpiece is equipped.
- [ ] Suppress local-radio playback when carrier is within ~3-5m to
  avoid awkward double-up doubling. Real radios have a slight delay
  between speech and over-air relay; we don't simulate that. Only
  noticeable in close-range testing — backlogged for now.
- [ ] Per-type voice variants — Police voice ≠ EMS voice ≠ HMMWV driver.
  Add a clip-set field per `RP_DispatchGroupDefinition`.
- [ ] Variation pool per line so it's not the same clip every time.
- [ ] Cooldown so spam-clicking the dispatch button doesn't talk over
  itself.
- [ ] Strip the `[RP_Dispatch] SoundEventTransform(...)` info-level Print
  in `PlaySoundOnCarriersRadioLocal` once polish work stabilises and the
  log noise outweighs the diagnostic value.
