# CNR 2026 Upgrade — Design

Source: Jarvis memory store, tags `arma`, `cnr`, `cops-n-robbers`,
`game-design`, `police-system` (created 2026-05-07).

## Summary

Integrated **Active Surveillance HUD** (LPR / Radar) and **Arrest /
Booking** lifecycle on top of the existing dispatch system.

## Phase status

- **Phase 1 — shipped (2026-05-16, PRs #6, #11).**
  - Radar / Speed detection (live MFD page, blank-when-closed, peak/lock
    flow). See `RP_SpeedRadar*Component` and the page-swap pattern
    captured in `reforger_setmaterial_breaks_ag0_rt_binding`.
  - ACE Captives cuff quickslot wiring on cop spawn
    (`RP_PlayerInventorySetupComponent` on the cop character prefab).
  - Yield-to-emergency for civilian AI (not originally in this doc;
    added during Phase 1). See `YIELD_TO_EMERGENCY_TASKS.md`.
  - Dispatch audio through a physical radio prop.
  - Traffic-cap admin panel injected into the pause menu.
- **Phase 2 — in progress.**
  - LPR (License Plate Reader) — scans plates of the radar's closest
    cone target. *(Shipped 2026-05-18, PR #19.)* Today the LPR shares
    the radar's "closest target" pick. Slice 3 (planned) adds a
    separate cone scan that flags any in-cone watched plate.
  - Watchlist match — red-alert when an LPR scan hits a flagged plate.
    *(Shipped 2026-05-18, PR #19.)* `RP_PlateWatchlistComponent` is
    server-authoritative on the GameMode, auto-budgets to a
    configurable percent of active traffic (default 5%, floor 1),
    and rebalances on every pool change. Radar `IsPlateFlagged()`
    consults it; the HUD distinguishes overspeed / LPR hit / both
    via `SPEEDING` and `WATCHLIST` badges, and an LPR hit also pops
    a `SCR_HintManagerComponent.ShowCustomHint` with the plate for
    the local cop driver.
  - Vehicle impound — Impound Sheet gadget + per-vehicle action
    teleports a car into a server-authoritative FIFO impound lot with
    slot recycling (driven-off cars freed, wrecks cleared, oldest
    evicted when full). *(Shipped, PR #22.)* See
    `RP_Impound*Component` / `RP_ImpoundVehicleUserAction`.
  - Jail hold + automated release at end of timer. *(Still planned.)*
  - In-game chat — history panel + channels enabled (fix landed via
    `ScriptedChatEntity` placement; see `CHAT_HUD_INVESTIGATION.md`).
- **Phase 3 — planned.**
  - Manual lookup interface — officer-driven plate / ID lookup as a
    fallback when LPR / radar don't trigger.

## Active Surveillance HUD

- **LPR (License Plate Reader)** — scans plates of nearby vehicles, red-
  alert match against a watchlist. *(Phase 2 — shipped 2026-05-18, PR #19.)*
- **Radar / Speed detection** — flags vehicles exceeding a speed
  threshold. *(Phase 1 — shipped.)*
- **Manual lookup interface** — officer-driven plate / ID lookup as a
  fallback when LPR / radar don't trigger. *(Phase 3)*

## Arrest / Booking lifecycle

- ACE-Captives-driven arrest loop. *(Phase 1 — cuff quickslot wired;
  arrest UX inherited from ACE Captives.)*
  - Cuffs sourced from `ACE_Captives_ZipCuffs.et` (already wired into
    cop loadout via `RP_PlayerInventorySetupComponent` quickslot
    setup on the cop character prefab).
- Jail hold with **automated release** at end of timer. *(Phase 2)*
