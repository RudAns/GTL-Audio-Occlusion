"""One-off: flag M_Probe for instanced static meshes (the probe markers are an ISM)."""
import unreal
m = unreal.load_asset("/Game/Occlusion/Materials/M_Probe")
m.set_editor_property("used_with_instanced_static_meshes", True)
unreal.MaterialEditingLibrary.recompile_material(m)
unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log("fix_probe_material: done")
