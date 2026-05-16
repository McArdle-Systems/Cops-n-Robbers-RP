# Speed Radar Unit — Project Handoff

**Project**: Cops n Robbers RP (Arma Reforger mod)
**Workbench project path**: `C:/Users/bluej/Documents/My Games/ArmaReforgerWorkbench/addons/cops-n-rc`
**Mod folder in Resource Browser**: `CrooksandPolice/`
**Component**: Dash-mounted speed radar unit (Stalker/Genesis style)
**Status**: 3D model imported and split into submeshes; materials created; material assignment pending re-export with per-part Blender materials.

---

## What's Done

### 1. 3D Model

- Generated procedurally via Blender Python script (`build_dash_radar.py`).
- Style: dash-mounted box, Stalker/Genesis radar unit aesthetic.
- Dimensions: ~16cm W × 8cm H × 9cm D.
- Origin at base of mounting puck, sits flat on dash surface.
- Layout:
  - **Antenna + lens** point along **-Y** (vehicle forward, through windshield).
  - **Screen + buttons** face **+Y** (toward driver).
  - **LED** on top, visible from any angle.

### 2. Mesh Hierarchy

Root empty: `SpeedRadar_DashUnit`

Child meshes:
- `SpeedRadar_Base` — flat puck, dash mount
- `SpeedRadar_Neck` — swivel cylinder
- `SpeedRadar_Housing` — main beveled box body
- `SpeedRadar_Screen` — driver-facing LCD plane
- `SpeedRadar_AntennaBody` — forward-pointing cylinder
- `SpeedRadar_AntennaLens` — emitter face disc
- `SpeedRadar_LED` — small status indicator on top
- `SpeedRadar_Button_0`, `_1`, `_2` — three buttons under screen

Total: 10 mesh objects, ~792 verts / 456 faces (current import).

### 3. Workbench Import State

- FBX imported from Blender → auto-converted to `.xob`.
- **First import attempt**: Merge Meshes was on by default → all geometry collapsed to single mesh named after `SpeedRadar_Housing`.
- **Fix**: Re-imported with **Merge Meshes UNCHECKED** in `.fbx` Import Settings → all 10 submeshes now visible in Details tab.
- **Current issue**: Material Assigns panel shows only 1 slot ("DefaultGeneratedMaterial.emat") because the original Blender export had no per-part material assignments. All meshes share one material slot.

### 4. Materials Created (.emat files)

All using **PBRBasic** shader. Stored in (assumed) `CrooksandPolice/Assets/Materials/SpeedRadar/`:

| Material file | BaseColor (RGBA) | Roughness | Metallic | EmissiveColor | EmissiveLV |
|---|---|---|---|---|---|
| `SpeedRadar_Plastic.emat` | 0.05, 0.05, 0.05, 1.0 | 0.6 | 0.0 | — | — |
| `SpeedRadar_Screen.emat` | 0.02, 0.02, 0.02, 1.0 | 0.2 | 0.0 | 1.0, 0.4, 0.0, 1.0 | 5.0 |
| `SpeedRadar_Glass.emat` | 0.1, 0.1, 0.12, 1.0 | 0.05 | 0.0 | — | — |
| `SpeedRadar_LED.emat` | 0.1, 0.0, 0.0, 1.0 | 0.3 | 0.0 | 1.0, 0.0, 0.0, 1.0 | 9.0 |
| `SpeedRadar_Metal.emat` | 0.4, 0.4, 0.42, 1.0 | 0.35 | 0.85 | — | — |

---

## What's Pending

### Immediate (mesh → material wiring)

1. Re-run **`build_dash_radar.py` v2** in Blender (assigns 5 unique Blender materials per part).
2. Re-export FBX, overwrite the existing one in Workbench.
3. Reimport `.xob` in Workbench. **Material Assigns** should now show **5 slots**:
   - `SpeedRadar_Plastic`
   - `SpeedRadar_Screen`
   - `SpeedRadar_Glass`
   - `SpeedRadar_LED`
   - `SpeedRadar_Metal`
4. Drag each `.emat` onto its matching slot.
5. Click **Reimport resource** at the top of Import Settings.

Material → mesh mapping (handled automatically by the v2 Blender script):
- `Plastic` → Base, Housing, Button_0/1/2
- `Metal` → Neck, AntennaBody
- `Glass` → AntennaLens
- `Screen` → Screen
- `LED` → LED

### Next Steps (after materials are wired)

1. **Create prefab** (`.et`) wrapping the `.xob`.
2. **Author SpeedRadarComponent** (Enforce script) — detects vehicles in a cone in front of the unit, reads their velocity, displays speed on a UI element. (Started early in conversation, not finished — see "Component Script Skeleton" below.)
3. **Attach component to the prefab** via Object Properties.
4. **Slot the prefab onto a vehicle dashboard** in the vehicle prefab hierarchy. Orient so the unit's -Y axis aligns with vehicle forward.
5. (Optional) **LODs**: currently only LOD 0. Reforger expects LOD 1, 2, 3 for distance fading. Can be auto-generated in Workbench or done manually in Blender.
6. (Optional) **Colliders**: none present (`Colliders (0)` in the .xob). For a dash-mounted item this likely doesn't matter, but if the radar should be shootable / interactable, add a simple box collider via Workbench's Geometry Editor or in Blender with a `UCX_` prefix on a low-poly hull.

---

## File Locations

### Source files (Blender)
- `build_dash_radar.py` v2 — generator script, last version creates per-part materials.
- `SpeedRadar.fbx` — exported from Blender (with per-part materials in v2).

### Workbench files (relative to mod root)
- `Assets/Models/SpeedRadar/SpeedRadar.fbx` — source FBX (path is assumed; user has not confirmed exact subfolder).
- `Assets/Models/SpeedRadar/SpeedRadar.xob` — auto-generated from FBX.
- `Assets/Materials/SpeedRadar/SpeedRadar_Plastic.emat`
- `Assets/Materials/SpeedRadar/SpeedRadar_Screen.emat`
- `Assets/Materials/SpeedRadar/SpeedRadar_Glass.emat`
- `Assets/Materials/SpeedRadar/SpeedRadar_LED.emat`
- `Assets/Materials/SpeedRadar/SpeedRadar_Metal.emat`

---

## Component Script Skeleton (not yet written/imported)

Started but interrupted earlier. Should live at something like:
`Scripts/Game/Components/SpeedRadarComponent.c`

Functional requirements discussed:
- Attach to any entity (radar prefab itself, or vehicle).
- Configurable detection radius and forward cone angle.
- Periodic scan (e.g. every 0.25s) for `Vehicle` entities in front cone.
- For each detected vehicle: read velocity vector magnitude → convert m/s to mph or km/h.
- Track fastest target in current scan.
- Expose current reading via component API for UI consumption.
- Network-replicate the reading (authority = server) so all clients see same value on the screen.

Pending decisions:
- Display strategy: world-space text decal on the screen mesh, or UI overlay when player looks at the unit?
- Units: mph vs. km/h (Cops n Robbers RP context — likely mph, US-style).
- Should the radar require power / be toggleable?

---

## Conventions Established

- **Forward axis**: -Y in Blender, exported with FBX axis settings Forward=-Y, Up=Z.
- **Units**: meters in Blender; "Apply Transform" enabled on FBX export so scale comes through 1:1.
- **Naming**: All radar parts prefixed `SpeedRadar_` for easy grouping/selection.
- **Material naming**: `SpeedRadar_<Type>.emat` — type matches Blender material name so slots auto-line-up.
- **Workbench version**: 1.6.0.119 (production).

---

## Known Gotchas Learned

1. **Merge Meshes on import** is ON by default — must be unchecked in `.fbx` Import Settings to keep submeshes separate.
2. **Material slots in .xob come from Blender material assignments**, NOT from having multiple mesh objects. Need unique Blender materials per part type to get multiple slots.
3. **Emissive in PBRBasic** uses `EmissiveColor` + `EmissiveLV` (intensity multiplier). No separate "Emissive" shader. EmissiveLV ~5–9 typical for visible glow.
4. **After editing emissive values**, may need to restart Workbench for preview to update (per BI wiki note on collimator materials).
5. **There is no MatPBREmissive shader** in current Workbench versions — `PBRBasic` handles everything via parameters.
6. **`.emat` files cannot be hand-authored externally** — must be created inside Workbench (proprietary GUID/reference wiring).

---

## Quick Resume Prompt for Claude Code

> I'm building a dash-mounted speed radar unit for an Arma Reforger mod (Cops n Robbers RP). The 3D model is generated via a Blender Python script and imported into Enfusion Workbench. Per-part materials are wired up via Blender material slots that map to .emat files in Workbench. I now need to (1) write the SpeedRadarComponent.c Enforce script that detects vehicles in a forward cone and reads their speed, (2) create a prefab wrapping the .xob with the component attached, and (3) figure out how to display the speed reading on the screen mesh. Read the handoff doc for current state.