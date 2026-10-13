"""Export the engine's footstep sample (NetworkPredictionExtras plugin content) to SourceAssets/Audio/Raw.
UnrealEditor-Cmd.exe AudioOcclusion.uproject -run=pythonscript -script=<abs>/Tools/export_engine_footstep.py -EnablePlugins=NetworkPredictionExtras
Then run Tools/make_step_sound.py (system Python) and Tools/import_step_sound.py (editor)."""
import os
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceAssets", "Audio", "Raw")
os.makedirs(OUT, exist_ok=True)
for n in ["Footstep_Rock_Land01"]:
    a = unreal.load_asset(f"/NetworkPredictionExtras/Audio/{n}")
    if not a:
        unreal.log(f"export_engine_footstep: missing {n}")
        continue
    t = unreal.AssetExportTask()
    t.object = a
    t.filename = os.path.join(OUT, n + ".wav")
    t.automated = True
    t.replace_identical = True
    t.prompt = False
    ok = unreal.Exporter.run_asset_export_task(t)
    unreal.log(f"export_engine_footstep: {n} {ok}")
