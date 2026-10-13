import unreal
out = []
def bounds(p):
    m = unreal.load_asset(p)
    if not m: out.append(f"MISSING {p}"); return
    b = m.get_bounding_box()
    out.append(f"{p}: min={b.min} max={b.max}")
for p in ["/Game/LevelPrototyping/Interactable/Door/Meshes/SM_Door","/Game/LevelPrototyping/Interactable/Door/Meshes/SM_DoorFrame_Edge","/Game/LevelPrototyping/Interactable/Door/Meshes/SM_DoorFrame_Corner","/Game/LevelPrototyping/Meshes/SM_Cube","/Game/LevelPrototyping/Meshes/SM_ChamferCube","/Game/LevelPrototyping/Meshes/SM_Cylinder","/Game/Building/Geometry/SM_Wall","/Game/Building/Geometry/SM_WallWindow","/Game/Building/Geometry/SM_FrameWindow","/Game/Building/Geometry/SM_StreetLight","/Game/Building/Geometry/SM_FlowerBed","/Game/Building/Geometry/SM_Flower_0","/Game/Building/Geometry/SM_DiscGround","/Game/Building/Geometry/SM_Land","/Game/ArchVis/SampleScene/Building/Meshes/Door_01_L","/Game/ArchVis/SampleScene/Tree/HillTree_02","/Game/Building/Geometry/SM_Beam","/Game/Building/Geometry/SM_Roof"]:
    bounds(p)
mel = unreal.MaterialEditingLibrary
for p in ["/Game/Building/Materials/MI_StdOpaque_Concrete","/Game/Building/Materials/MI_White","/Game/Building/Materials/MI_Slab","/Game/Building/Materials/MI_Simple_Color","/Game/Building/Materials/MI_Vivid","/Game/Building/Materials/MI_Structure","/Game/Building/Materials/MI_Land","/Game/Building/Materials/MI_Columns","/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray","/Game/LevelPrototyping/Materials/MI_DefaultColorway","/Game/Building/Materials/MI_StdOpaque_Brushed","/Game/Building/Materials/MI_Roof"]:
    mi = unreal.load_asset(p)
    if not mi: out.append(f"MISSING {p}"); continue
    parent = mi.get_editor_property("parent")
    sc = [ (str(x.parameter_info.name), x.parameter_value) for x in mi.get_editor_property("scalar_parameter_values")]
    vc = [ (str(x.parameter_info.name), x.parameter_value) for x in mi.get_editor_property("vector_parameter_values")]
    tx = [ (str(x.parameter_info.name), x.parameter_value.get_name() if x.parameter_value else None) for x in mi.get_editor_property("texture_parameter_values")]
    allp = mel.get_scalar_parameter_names(mi) if hasattr(mel,'get_scalar_parameter_names') else []
    out.append(f"{p} parent={parent.get_path_name() if parent else None}\n   S={sc}\n   V={vc}\n   T={tx}\n   allS={list(allp)} allV={list(mel.get_vector_parameter_names(mi))}")
open(unreal.Paths.project_saved_dir()+"inspect.txt","w").write("\n".join(out))
