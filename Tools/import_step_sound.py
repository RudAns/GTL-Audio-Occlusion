"""One-off: import SW_Step.wav as a non-looping sound (same settings as build_assets.import_sounds)."""
import os, unreal
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
t = unreal.AssetImportTask()
t.filename = os.path.join(PROJ, "SourceAssets", "Audio", "SW_Step.wav")
t.destination_path = "/Game/Occlusion/Audio"
t.replace_existing = True; t.automated = True; t.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
w = unreal.load_asset("/Game/Occlusion/Audio/SW_Step")
w.set_editor_property("looping", False)
w.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
unreal.EditorAssetLibrary.save_loaded_asset(w)
unreal.log("import_step_sound: done")
