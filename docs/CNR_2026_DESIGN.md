# CNR 2026 Upgrade — Design

Source: Jarvis memory store, tags `arma`, `cnr`, `cops-n-robbers`,
`game-design`, `police-system` (created 2026-05-07).

## Summary

Integrated **Active Surveillance HUD** (LPR / Radar) and **Arrest /
Booking** lifecycle on top of the existing dispatch system.

## Active Surveillance HUD

- **LPR (License Plate Reader)** — scans plates of nearby vehicles, red-
  alert match against a watchlist.
- **Radar / Speed detection** — flags vehicles exceeding a speed
  threshold.
- **Manual lookup interface** — officer-driven plate / ID lookup as a
  fallback when LPR / radar don't trigger.

## Arrest / Booking lifecycle

- ACE-Captives-driven arrest loop.
  - Cuffs sourced from `ACE_Captives_ZipCuffs.et` (already wired into
    cop loadout via `RP_GameMode.OnPlayerSpawned` quickslot setup).
- Jail hold with **automated release** at end of timer.
