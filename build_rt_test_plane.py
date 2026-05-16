"""
Generates a big vertical 1.5m x 1m plane for the RT-to-mesh test.
Run once in Blender's Scripting tab. Auto-exports to Assets/Models/SpeedRadarV2/RTTest_Plane.fbx.

Plane is upright (facing -Y in Blender = forward in Reforger), so the
test entity placed at world coords renders the test face toward you.
"""

import bpy
import os

EXPORT_DIR = r"C:\Users\bluej\Documents\My Games\ArmaReforgerWorkbench\addons\cops-n-robbers-rp\Assets\Models\SpeedRadarV2"

# -----------------------------------------------------------------------------
# Cleanup previous run
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.data.objects):
    if obj.name.startswith("RTTest"):
        bpy.data.objects.remove(obj, do_unlink=True)
for mat in list(bpy.data.materials):
    if mat.name == "RTTest_Surface":
        bpy.data.materials.remove(mat, do_unlink=True)

if bpy.context.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

# -----------------------------------------------------------------------------
# Build the plane: 1.5m wide × 1m tall, standing upright at z=1m
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_plane_add(size=1.0, location=(0, 0, 1.0))
plane = bpy.context.active_object
plane.name = "RTTest_Plane"
plane.scale = (1.5, 1.0, 1.0)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

# Stand it up — rotate so the plane normal points along -Y (forward)
plane.rotation_euler = (1.5708, 0, 0)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

# Material so the .xob has a named slot for the .emat to assign against
mat = bpy.data.materials.new(name="RTTest_Surface")
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get("Principled BSDF")
if bsdf:
    bsdf.inputs["Base Color"].default_value = (1, 1, 1, 1)
plane.data.materials.append(mat)

# -----------------------------------------------------------------------------
# Export
# -----------------------------------------------------------------------------
os.makedirs(EXPORT_DIR, exist_ok=True)
bpy.ops.object.select_all(action='DESELECT')
plane.select_set(True)
bpy.context.view_layer.objects.active = plane

out_path = os.path.join(EXPORT_DIR, "RTTest_Plane.fbx")
bpy.ops.export_scene.fbx(
    filepath=out_path,
    use_selection=True,
    apply_unit_scale=True,
    bake_space_transform=True,
    axis_forward='-Y',
    axis_up='Z',
    object_types={'MESH'},
)
print(f"=== Exported {out_path} ===")
