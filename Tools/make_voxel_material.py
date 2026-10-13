"""One-off: create only M_Voxel (same code as build_assets.make_voxel) without rebuilding everything else."""
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location("build_assets_mod", os.path.join(os.path.dirname(__file__), "build_assets.py"))
src = open(spec.origin, encoding="utf-8").read().replace("\nmain()", "\n")
ns = {"__file__": spec.origin}
exec(compile(src, spec.origin, "exec"), ns)
ns["make_voxel"]()
ns["log"]("make_voxel_material: done")
