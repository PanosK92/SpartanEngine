"""Keep portable authoring sources and bridge helpers inside the Dropbox project."""
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'binaries/project/exo_chora'
names=['build_exo_chora.py','preview_exo_chora.py','install_exo_chora.py','fetch_village_textures.py',
       'import_village_meshes.mjs','village_bridge.mjs','review_exo_chora.mjs','inspect_village_site.py',
       'verify_exo_chora.py','check_village_routes.mjs','package_exo_chora.py']
for name in names:
    text=(Path(__file__).resolve().parent/name).read_text(encoding='utf-8')
    text=text.replace('Path(__file__).resolve().parents[2]',"next(p for p in Path(__file__).resolve().parents if (p/'binaries/project').is_dir())")
    text=text.replace("../mcp/spartan_engine/engine_client.mjs","./bridge/engine_client.mjs")
    (OUT/'sources'/name).write_text(text,encoding='utf-8')
bridge=OUT/'sources/bridge';bridge.mkdir(exist_ok=True)
for name in ['engine_client.mjs','debug_log.mjs']:
    src=ROOT/'tools/mcp/spartan_engine'/name
    if src.exists() and src.resolve()!=(bridge/name).resolve():shutil.copy2(src,bridge/name)
shutil.copy2(ROOT/'worlds/plan.world',OUT/'sources/plan_with_exo_chora.world')
shutil.copy2(ROOT/'binaries/project/plan_resources/terrain_sculpt.bin',OUT/'sources/terrain_sculpt_with_village.bin')
print('Portable sources, original and installed worlds, and both terrain revisions saved in project/exo_chora/sources.')
