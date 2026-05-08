"""
Dash-mounted speed radar (Stalker/Genesis style) generator for Blender.
v3: Splits export into THREE FBXs so LED and Screen become their own .xob —
    needed because the Reforger script API can only swap materials at the
    entity level, not per-slot on a multi-material MeshObject.

Run in Blender's Scripting tab. If EXPORT_DIR is set, the script writes
three FBXs automatically at the end:
  - SpeedRadarBody.fbx   (all the always-on parts: housing, antenna, etc.)
  - SpeedRadarLED.fbx    (just the LED bulb — runtime material swap)
  - SpeedRadarScreen.fbx (just the screen face — runtime material swap)

If EXPORT_DIR is None, the script just builds the geometry and leaves
everything selected for manual File > Export > FBX.

The 5 materials created here are PLACEHOLDERS. After import in Workbench,
the body .xob has 3 slots (Plastic / Glass / Metal); the LED and Screen
.xobs each have 1 slot. Assign real .emat files in Material Assigns.
"""

import bpy
import os

# Set to None to skip auto-export (build only, then export manually).
# Otherwise point at the Assets/Models folder under the mod project.
EXPORT_DIR = r"C:\Users\bluej\Documents\My Games\ArmaReforgerWorkbench\addons\cops-n-robbers-rp\Assets\Models\SpeedRadarV2"

# -----------------------------------------------------------------------------
# Cleanup previous run
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.data.objects):
    if obj.name.startswith("SpeedRadar"):
        bpy.data.objects.remove(obj, do_unlink=True)
for mat in list(bpy.data.materials):
    if mat.name.startswith("SpeedRadar_"):
        bpy.data.materials.remove(mat, do_unlink=True)

if bpy.context.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

created_parts = []

# -----------------------------------------------------------------------------
# Materials — placeholder Blender materials = Enfusion material slots
# -----------------------------------------------------------------------------
def make_material(name, color):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    return mat

mat_plastic = make_material("SpeedRadar_Plastic", (0.05, 0.05, 0.05))
mat_screen  = make_material("SpeedRadar_Screen",  (1.0, 0.4, 0.0))
mat_glass   = make_material("SpeedRadar_Glass",   (0.1, 0.1, 0.12))
mat_led     = make_material("SpeedRadar_LED",     (1.0, 0.0, 0.0))
mat_metal   = make_material("SpeedRadar_Metal",   (0.4, 0.4, 0.42))

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
def assign_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)

def add_box(name, size, location, material, rotation=(0, 0, 0)):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.mesh.primitive_cube_add(size=1, location=location, rotation=rotation)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    assign_material(obj, material)
    created_parts.append(obj)
    return obj

def add_cylinder(name, radius, depth, location, material, rotation=(0, 0, 0)):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.mesh.primitive_cylinder_add(
        radius=radius, depth=depth, vertices=16,
        location=location, rotation=rotation
    )
    obj = bpy.context.active_object
    obj.name = name
    assign_material(obj, material)
    created_parts.append(obj)
    return obj

def bevel_object(obj, width=0.002, segments=2):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.bevel(offset=width, segments=segments)
    bpy.ops.object.mode_set(mode='OBJECT')

# -----------------------------------------------------------------------------
# Build parts (each gets its material assigned at creation)
# -----------------------------------------------------------------------------
add_cylinder("SpeedRadar_Base", 0.035, 0.012, (0, 0, 0.006), mat_plastic)
add_cylinder("SpeedRadar_Neck", 0.012, 0.025, (0, 0, 0.024), mat_metal)

housing = add_box("SpeedRadar_Housing", (0.16, 0.06, 0.05),
                  (0, 0, 0.062), mat_plastic)
bevel_object(housing, 0.004, 2)

# Driver-facing screen (+Y)
add_box("SpeedRadar_Screen", (0.10, 0.001, 0.032),
        (0, 0.031, 0.065), mat_screen)

# Antenna points forward (-Y)
add_cylinder("SpeedRadar_AntennaBody", 0.022, 0.04,
             (0, -0.075, 0.062), mat_metal,
             rotation=(1.5708, 0, 0))

add_cylinder("SpeedRadar_AntennaLens", 0.020, 0.002,
             (0, -0.096, 0.062), mat_glass,
             rotation=(1.5708, 0, 0))

add_cylinder("SpeedRadar_LED", 0.004, 0.004,
             (0.06, 0, 0.090), mat_led)

for i, x in enumerate((-0.04, 0.0, 0.04)):
    add_box(f"SpeedRadar_Button_{i}", (0.012, 0.003, 0.008),
            (x, 0.031, 0.042), mat_plastic)

# -----------------------------------------------------------------------------
# Parent everything
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, 0, 0))
root = bpy.context.active_object
root.name = "SpeedRadar_DashUnit"

for obj in created_parts:
    obj.parent = root

# -----------------------------------------------------------------------------
# Final selection for export
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
root.select_set(True)
for obj in created_parts:
    obj.select_set(True)
bpy.context.view_layer.objects.active = root

# Sanity report
mesh_count = sum(1 for o in created_parts if o.type == 'MESH')
unique_mats = set()
for obj in created_parts:
    for slot in obj.material_slots:
        if slot.material:
            unique_mats.add(slot.material.name)

print("=== Build complete ===")
print(f"Mesh parts: {mesh_count}")
print(f"Unique materials: {len(unique_mats)}")
for m in sorted(unique_mats):
    print(f"  - {m}")

# -----------------------------------------------------------------------------
# Auto-export — three FBXs so LED / Screen each become their own .xob.
# Body keeps the multi-slot mesh (those parts never toggle at runtime).
# -----------------------------------------------------------------------------
LED_PARTS    = ["SpeedRadar_LED"]
SCREEN_PARTS = ["SpeedRadar_Screen"]
# Body = everything else (we compute it after the build so any part not
# explicitly listed above defaults to body — newly-added body parts don't
# need to be re-listed here).
BODY_PARTS = [
    o.name for o in created_parts
    if o.name not in LED_PARTS and o.name not in SCREEN_PARTS
]

def export_subset(filepath, part_names):
    bpy.ops.object.select_all(action='DESELECT')
    for n in part_names:
        obj = bpy.data.objects.get(n)
        if obj:
            obj.select_set(True)
    if not any(o.select_get() for o in bpy.data.objects):
        print(f"  [skip] No parts selected for {filepath}")
        return
    bpy.context.view_layer.objects.active = bpy.data.objects[part_names[0]]
    bpy.ops.export_scene.fbx(
        filepath=filepath,
        use_selection=True,
        apply_unit_scale=True,
        bake_space_transform=True,
        axis_forward='-Y',
        axis_up='Z',
        object_types={'MESH'},
    )
    print(f"  [ok] {filepath}")

if EXPORT_DIR:
    print("\n=== Exporting 3 FBXs ===")
    os.makedirs(EXPORT_DIR, exist_ok=True)
    export_subset(os.path.join(EXPORT_DIR, "SpeedRadarBody.fbx"),   BODY_PARTS)
    export_subset(os.path.join(EXPORT_DIR, "SpeedRadarLED.fbx"),    LED_PARTS)
    export_subset(os.path.join(EXPORT_DIR, "SpeedRadarScreen.fbx"), SCREEN_PARTS)
    # Re-select everything so the user can still inspect / re-export by hand.
    bpy.ops.object.select_all(action='DESELECT')
    root.select_set(True)
    for obj in created_parts:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = root
else:
    print("\nEXPORT_DIR is None — skipping auto-export.")
    print("Manual: File > Export > FBX with 'Selected Objects' checked.")