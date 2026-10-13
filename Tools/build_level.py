"""Headless level build: new map, sound house, Steam Audio geometry export.
UnrealEditor-Cmd.exe AudioOcclusion.uproject -run=pythonscript -script=<abs>/Tools/build_level.py"""
import unreal

EAL = unreal.EditorAssetLibrary
MAP = "/Game/Maps/SoundHouse"
SA = "/Game/Occlusion/SteamAudio"


def log(*a):
    unreal.log("build_level: " + " ".join(str(x) for x in a))


def main():
    if EAL.does_asset_exist(MAP):
        EAL.delete_asset(MAP)
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    n = unreal.OcclusionBuildLibrary.build_sound_house(world)
    log("spawned", n)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    log("map saved", ok)
    exported = unreal.OcclusionBuildLibrary.export_steam_audio(world, f"{SA}/SoundHouse_Static.SoundHouse_Static", f"{SA}/Doors")
    log("steam audio export", exported)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("map saved again", ok, "ALL DONE")


main()
