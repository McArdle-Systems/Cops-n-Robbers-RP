# Cops n Robbers RP

Arma Reforger mission. RP-style police / civilian sandbox.

Design lives in `~/.openclaw/workspace-main/RP_MISSION_DESIGN.md` and `RP_DEPENDENCIES.md`.

## Phase 0a — AI Movement POC

First proving-ground build. Does the engine let us spawn infantry + a vehicle and tell them "move to me" reliably?

### What this POC does

- One game-mode component (`RP_AIMovePOCComponent`) attached to the GameMode entity in a test world.
- On player join, spawns:
  - One infantry group ~80m from the player.
  - One vehicle with an AI crew ~80m from the player.
- Listens for two debug menu items (DiagMenu, default key `Backtick`):
  - **RP > AI POC > Move Infantry To Me** — issues a Move waypoint to the spawned infantry group with the player's current position.
  - **RP > AI POC > Move Vehicle To Me** — issues a Move waypoint to the spawned vehicle group with the player's current position.

### Setup in Workbench

1. Open this project as an addon in the Reforger Workbench.
2. Open `Missions/AI_POC.conf` (or any test world / scenario).
3. On the GameMode entity, add the `RP_AIMovePOCComponent`.
4. Set the prefab attributes from the Resource Browser:
   - **Infantry Group Prefab** — e.g. `Prefabs/Groups/BLUFOR/Group_US_Team.et`
   - **Vehicle Prefab** — e.g. `Prefabs/Vehicles/Wheeled/M151A2/M151A2.et` (any drivable vehicle)
   - **Vehicle Crew Group Prefab** — small group prefab with 1–2 AI (driver + optional gunner)
   - **Move Waypoint Prefab** — `Prefabs/AI/Waypoints/AIWaypoint_Move.et` (engine default; locate via Resource Browser)
   - **Get-In Waypoint Prefab** — `Prefabs/AI/Waypoints/AIWaypoint_GetIn.et`
5. Press Play. Open DiagMenu (`Backtick`). Navigate to **RP > AI POC**.

### Promotion criteria (move to Phase 0)

- Infantry consistently pathfinds to the player from any position in town.
- Vehicle group boards the vehicle and drives to the player without getting stuck on parked cars or low walls > 50% of the time.
- No script errors on respawn / repeat command.
- Confirmed working with Better Everon enabled (the Phase 0a target map).

## Layout

```
addon.gproj            Project definition + dependency on ArmaReforger core
Scripts/Game/AI/       Custom AI scripts
Common/Inputs/         (future) custom input bindings
Configs/               Game mode + component configs
Prefabs/AI/            (future) authored AI group / vehicle prefabs
Missions/              Test scenarios for the workbench
docs/                  Design notes, references
```

## License

This work is licensed under the [Arma Public License Share Alike (APL-SA)](https://www.bohemia.net/community/licenses/arma-public-license-share-alike).
See [LICENSE.txt](LICENSE.txt). Use must be NonCommercial and ArmaOnly;
derivatives must be released under APL-SA. Attribution is required.
