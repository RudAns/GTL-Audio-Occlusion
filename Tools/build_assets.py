"""Headless asset build: sounds, materials, Steam Audio materials.
UnrealEditor-Cmd.exe AudioOcclusion.uproject -run=pythonscript -script=<abs>/Tools/build_assets.py"""
import os
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MAT = "/Game/Occlusion/Materials"
AUD = "/Game/Occlusion/Audio"
SA = "/Game/Occlusion/SteamAudio"


def log(*a):
    unreal.log("build_assets: " + " ".join(str(x) for x in a))


def fresh(name, folder, cls, factory):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return TOOLS.create_asset(name, folder, cls, factory)


# ------------------------------------------------------------------ sounds
def import_sounds():
    src = os.path.join(PROJ, "SourceAssets", "Audio")
    tasks = []
    for f in sorted(os.listdir(src)):
        if not f.lower().endswith(".wav"):
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, f)
        t.destination_path = AUD
        t.replace_existing = True
        t.automated = True
        t.save = False
        tasks.append(t)
    TOOLS.import_asset_tasks(tasks)
    paths = [f"{AUD}/{os.path.splitext(os.path.basename(t.filename))[0]}" for t in tasks] + ["/Game/XRFramework/Audio/Fire01"]
    for p in paths:
        w = unreal.load_asset(p)
        if not w:
            log("missing sound", p)
            continue
        # SW_Step is a one-shot re-triggered on every foot plant; everything else loops.
        w.set_editor_property("looping", not p.endswith("/SW_Step"))
        # Inactive solution voices are kept nearly silent; they must keep playing so all voices stay in sync.
        w.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
        EAL.save_loaded_asset(w)
        log("sound", p)


# ------------------------------------------------------------------ materials
class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.x = -600

    def node(self, cls, **props):
        n = MEL.create_material_expression(self.mat, cls, self.x, 0)
        self.x += 40
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n


def make_emissive_master():
    mat = fresh("M_Emissive", MAT, unreal.Material, unreal.MaterialFactoryNew())
    g = Graph(mat)
    col = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Color", default_value=unreal.LinearColor(4, 3, 2, 1))
    base = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Base", default_value=unreal.LinearColor(0.05, 0.05, 0.05, 1))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_probe():
    mat = fresh("M_Probe", MAT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    g = Graph(mat)
    col = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Color", default_value=unreal.LinearColor(0.5, 2, 2, 1))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def make_voxel():
    """Lit material for the instanced voxel cubes of the wave bake view (needs the ISM usage flag)."""
    mat = fresh("M_Voxel", MAT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    g = Graph(mat)
    col = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Color", default_value=unreal.LinearColor(0.8, 0.8, 0.82, 1))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = g.node(unreal.MaterialExpressionConstant, r=0.7)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def make_data_slice():
    mat = fresh("M_DataSlice", MAT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    g = Graph(mat)
    tex = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Data",
                 texture=unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    gain = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="Gain", default_value=1.6)
    mul = g.node(unreal.MaterialExpressionMultiply)
    MEL.connect_material_expressions(tex, "RGB", mul, "A")
    MEL.connect_material_expressions(gain, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def mi(name, parent, vectors=None, scalars=None):
    inst = fresh(name, MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    inst.set_editor_property("parent", parent)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(inst, k, unreal.LinearColor(*v))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(inst, k, v)
    MEL.update_material_instance(inst)
    EAL.save_loaded_asset(inst)
    return inst


def make_materials():
    simple = unreal.load_asset("/Game/Building/Materials/Master/M_Simple_Opaque")
    concrete = unreal.load_asset("/Game/Building/Materials/MI_StdOpaque_Concrete")
    glass = unreal.load_asset("/Game/Building/Materials/MI_Simple_Glass_Inst")
    emissive = make_emissive_master()
    make_probe()
    make_data_slice()
    make_voxel()
    S = lambda n, c, r=0.6, m=0.0: mi(n, simple, {"Color": (*c, 1)}, {"Roughness": r, "Metallic": m})
    C = lambda n, tint, r=0.8: mi(n, concrete, {"TintColor": (*tint, 1)}, {"RoughnessAmount": r})
    E = lambda n, c: mi(n, emissive, {"Color": (*c, 1)})
    C("MI_Exterior", (0.88, 0.84, 0.78), 0.85)
    C("MI_Floor", (0.62, 0.58, 0.54), 0.45)
    C("MI_Stone", (0.72, 0.70, 0.68), 0.9)
    C("MI_Column", (0.80, 0.78, 0.74), 0.8)
    S("MI_Plaster", (0.86, 0.83, 0.78), 0.85)
    S("MI_Ceiling", (0.90, 0.89, 0.86), 0.9)
    S("MI_Partition", (0.60, 0.40, 0.24), 0.6)
    S("MI_DoorWood", (0.34, 0.20, 0.11), 0.5)
    S("MI_Frame", (0.06, 0.06, 0.07), 0.45)
    S("MI_Metal", (0.78, 0.78, 0.80), 0.3, 1.0)
    S("MI_Wood", (0.52, 0.34, 0.19), 0.55)
    S("MI_WoodDark", (0.20, 0.13, 0.08), 0.5)
    S("MI_FabricTeal", (0.10, 0.38, 0.38), 0.95)
    S("MI_FabricRust", (0.52, 0.18, 0.09), 0.95)
    S("MI_Rug", (0.50, 0.40, 0.28), 1.0)
    S("MI_RugBlue", (0.12, 0.18, 0.34), 1.0)
    S("MI_Piano", (0.015, 0.015, 0.02), 0.12)
    S("MI_Machine", (0.22, 0.42, 0.33), 0.45, 0.5)
    S("MI_Barrel", (0.55, 0.18, 0.07), 0.5, 0.3)
    S("MI_Pot", (0.62, 0.35, 0.22), 0.8)
    S("MI_Leaves", (0.10, 0.32, 0.10), 0.9)
    S("MI_Grass", (0.20, 0.30, 0.13), 0.95)
    S("MI_RadioBody", (0.55, 0.30, 0.16), 0.45)
    S("MI_Speaker", (0.07, 0.07, 0.08), 0.6)
    S("MI_Accent_Hall", (0.08, 0.50, 0.50), 0.5)
    S("MI_Accent_Study", (0.22, 0.48, 0.18), 0.5)
    S("MI_Accent_Corridor", (0.35, 0.35, 0.38), 0.5)
    S("MI_Accent_Music", (0.18, 0.28, 0.70), 0.5)
    S("MI_Accent_Workshop", (0.85, 0.42, 0.10), 0.5)
    E("MI_Glow_Warm", (9, 5.5, 2.6))
    E("MI_Glow_Cool", (1.0, 3.0, 8.0))
    E("MI_Fire", (30, 8, 1.2))
    E("MI_ClockFace", (1.6, 1.55, 1.4))
    if glass:
        g = fresh("MI_Glass", MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        g.set_editor_property("parent", glass)
        EAL.save_loaded_asset(g)
    log("materials done")


# ------------------------------------------------------------------ Steam Audio materials
def make_steam_materials():
    # Transmission values are amplitude gains (Steam Audio applies them as EQ), converted from the same per-band
    # transmission losses the custom pipeline uses (OcclusionTypes.h), so both solutions hear the same walls.
    db = lambda *tl: [10 ** (-x / 20.0) for x in tl]
    defs = {
        "SA_Concrete": ((0.05, 0.07, 0.08), 0.05, db(42, 52, 60)),
        "SA_Wood": ((0.11, 0.07, 0.06), 0.05, db(14, 22, 30)),
        "SA_Glass": ((0.06, 0.03, 0.02), 0.05, db(18, 26, 30)),
        "SA_Door": ((0.10, 0.08, 0.06), 0.05, db(16, 22, 27)),
        "SA_Ground": ((0.30, 0.50, 0.60), 0.4, db(60, 60, 60)),
    }
    for name, (ab, sc, tr) in defs.items():
        m = fresh(name, SA, unreal.SteamAudioMaterial, unreal.SteamAudioMaterialFactory())
        m.set_editor_property("absorption_low", ab[0]); m.set_editor_property("absorption_mid", ab[1]); m.set_editor_property("absorption_high", ab[2])
        m.set_editor_property("scattering", sc)
        m.set_editor_property("transmission_low", tr[0]); m.set_editor_property("transmission_mid", tr[1]); m.set_editor_property("transmission_high", tr[2])
        EAL.save_loaded_asset(m)
        log("steam material", name, [round(x, 4) for x in tr])


def main():
    import_sounds()
    make_materials()
    make_steam_materials()
    log("ALL DONE")


main()
