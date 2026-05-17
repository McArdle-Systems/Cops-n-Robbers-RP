# Cops n Robbers RP — Mod Dependencies

Compiled 2026-04-29 from Workshop survey. Versions reflect "latest at survey time" — pin in mission `addon.gproj` once selected. Re-survey before Phase 0a build kickoff in case any of these moved.

## Runtime dependencies (Adopt — load alongside mission)

| Mod | License | Workshop ID | Purpose |
|---|---|---|---|
| EnfusionPersistenceFramework | MIT | `5D6EBC81EB1842EF` | Save/load layer (de facto standard, by Arkensor) |
| EnfusionDatabaseFramework | MIT | `5D6EA74A94173EDF` | DB backend; required by EveronLife and the Persistence Framework |
| ACE Captives | GPLv2 | `646D52AF8BB3FF15` | Surrender, zip cuffs, frisk, restrained state |
| Reloadz Vehicles | APL-ND | `60CAFAA76E027A42` | Civilian + police vehicle pack |
| Reloadz Police | APL-ND | `629E31161BF14984` | LEO uniforms / arsenal |
| Sheriff Pack - Reloadz | APL-ND | (lookup at integration time) | Sheriff variant + Impound Sheet user-action |
| Vinny - Civilians | APL-SA | `61A0D54519634294` | Civilian + militia skin variety |
| Domins Civilian Expansion | APL | `630B3E95B52F31DF` | Paramedics, firefighters, hunters |
| Better Everon | APL | `6336599C3E083CF6` | Town densification (St Pierre, Regina, Levie, etc.) |

**License note:** APL-ND mods (Reloadz family) can be loaded as runtime dependencies but **cannot be modified or repackaged**. Plan integrations around their public API only.

## Forks (carry source in our repo, modify freely)

| Source | License | Notes |
|---|---|---|
| `github.com/ekudmada/Reforger-Shop-System` | APL-SA | Currency + buy UI (513K Workshop downloads). Extend with sell flow per upstream roadmap. |

## Reference (read source, don't fork)

| Source | Why |
|---|---|
| `github.com/EveronLife/EveronLife` (MIT) | Worked example of EPF + EDF persistence wiring. Hands-on inspection 2026-04-29 showed it's a thin tech demo (one barter crate, one juicer in DebugWorld, recursion bug in split-quantity UI), not a framework. Build clean; consult source when stuck on persistence integration gotchas. |

## Reference (study, don't import)

| Mod | Why useful |
|---|---|
| Cops V Gangs Life (Workshop `65B72A84F7056795`) | Closest existing concept match; built on EveronLife stack |
| Aurora Life (Workshop `646114D7DD753EBF`) | Civ/cop/medic/criminal split validation |
| Police Sirene (Workshop `5D1152CFD8FF188F`) | Audio/light component reference (stale Jun 2023, APL-SA) |
| Encounter Project | Vendor system patterns (APL-ND blocks reuse) |

## Custom (no Workshop coverage — build from scratch)

- Civilian idle / panic / 911-call AI
- Traffic system (road graph, density manager, lane discipline)
- AI Director (event scheduler, intensity curve, spatial-relevance picker)
- Dispatch / 911 / ticketing UIs
- NPC dialogue trees (engine primitives only — `SCR_ConfigurableDialogUI`, `ChimeraMenuBase`)
- Heat / wanted system
- Jail / booking lifecycle (cuffing exists via ACE Captives; the lifecycle around it doesn't)

## Per-phase loadout

| Phase | Adds |
|---|---|
| **0a — POC** | Vinny, Domins, Better Everon |
| **0 — Living Town** | + EnfusionPersistenceFramework, Reloadz Vehicles |
| **1 — Crime Happens** | + ACE Captives, Reloadz Police, Sheriff Pack |
| **2 — Money & Robber Side** | + Shop System (forked) |
| **3 — Friends Mode** | All locked in; pin versions |
| **4 — Soft Public** | All; admin tools added |

## Version pinning policy

Pin everything before Phase 1 ships. Reforger Workshop mods auto-update for clients, which means a dependency push can break our mission silently. When pinning, snapshot the Workshop version IDs in this file and any in-mission manifest. Re-test before bumping.
