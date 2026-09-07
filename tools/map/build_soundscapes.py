"""Author eight acoustic districts and a terrain-derived shoreline in plan.world.

Requires numpy, scipy, matplotlib, pillow, shapely, soundfile. Run
python tools/audio_tests/fetch_ambience.py first, then this script --apply.
The heightmap and atlas are existing project assets. No gameplay areas are moved.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path
import shutil
import struct
import xml.etree.ElementTree as ET

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import resample_poly, butter, sosfiltfilt
from scipy.ndimage import binary_fill_holes
import soundfile as sf
from shapely.geometry import Polygon, Point
from shapely.ops import unary_union

from osm_roads import Projection

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'binaries/project/soundscapes'
CACHE = ROOT / 'binaries/audio_tests/ambience_sources'
WORLD = ROOT / 'worlds/plan.world'
RATE = 32000
# Design regions, not administrative boundaries. Lon/lat anchors are registered
# through the existing atlas so its compressed north/south scale is respected.
REGIONS = [
    ('highlands', 'Vrachionas and western villages', 20.735, 37.815, {'wind': .62, 'cicadas': .19, 'birds': .06}),
    ('north', 'Volimes and Skinari', 20.690, 37.901, {'wind': .72, 'cicadas': .09, 'birds': .05}),
    ('east', 'Alykes, Tragaki and Tsilivi', 20.810, 37.842, {'wind': .22, 'cicadas': .40, 'birds': .16}),
    ('plain', 'Central olive plain', 20.814, 37.772, {'wind': .18, 'cicadas': .55, 'birds': .19, 'village': .06}),
    ('south', 'Laganas, Kalamaki and airport plain', 20.882, 37.744, {'wind': .30, 'cicadas': .25, 'birds': .09}),
    ('vasilikos', 'Skopos and Vasilikos', 20.978, 37.720, {'wind': .20, 'cicadas': .44, 'birds': .24}),
    ('keri', 'Keri and the southwest', 20.816, 37.675, {'wind': .47, 'cicadas': .30, 'birds': .12}),
    ('town', 'Zakynthos Town and Bochali', 20.898, 37.786, {'wind': .14, 'birds': .07}),
]

def parts(geometry):
    if geometry.is_empty: return []
    if geometry.geom_type == 'Polygon': return [geometry]
    return [p for g in geometry.geoms for p in parts(g)]

def load_sources():
    records = json.loads((CACHE / 'sources.json').read_text())
    sounds = {}
    for entry in records:
        data, rate = sf.read(ROOT / entry['file'], always_2d=True, dtype='float32')
        assert data.size and np.isfinite(data).all()
        if data.shape[1] == 1: data = np.repeat(data, 2, axis=1)
        data = data[:, :2]
        divisor = math.gcd(rate, RATE)
        data = resample_poly(data, RATE // divisor, rate // divisor, axis=0)
        data = sosfiltfilt(butter(2, 90, btype='highpass', fs=RATE, output='sos'), data, axis=0)
        # Match source energy before artistic mixing, preserve dynamics and stereo.
        data *= .065 / max(float(np.sqrt(np.mean(data * data))), 1e-6)
        data *= min(1.0, .6 / max(float(np.abs(data).max()), 1e-6))
        sounds.setdefault(entry['name'], []).append(data)
    return sounds, records

def tile_loop(data, frames, offset=0):
    # Overlap-add the tail into the head. The resulting join follows consecutive
    # original samples rather than fading to silence at every loop boundary.
    cross = min(RATE * 2, len(data) // 4)
    fade = np.linspace(0, 1, cross, endpoint=False)[:, None]
    loop = np.concatenate([data[-cross:] * (1-fade) + data[:cross] * fade, data[cross:-cross]])
    return loop[(np.arange(frames) + offset) % len(loop)]

def write_loop(path, data):
    data = tile_loop(data, len(data) - 2 * RATE)
    assert np.isfinite(data).all() and np.max(np.abs(data)) < .95
    sf.write(path, data, RATE, subtype='PCM_16')
    return dict(file=path.name, seconds=round(len(data)/RATE, 3), peak_db=round(20*np.log10(max(np.abs(data).max(),1e-9)), 2), rms_db=round(20*np.log10(np.sqrt(np.mean(data*data))), 2), sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def half_plane(a, b):
    middle = (a+b)*.5
    normal = (b-a)/np.linalg.norm(b-a)
    tangent = np.array([-normal[1],normal[0]])
    return Polygon([middle+tangent*100000, middle-tangent*100000, middle-tangent*100000-normal*200000, middle+tangent*100000-normal*200000])

def terrain_grid(projection, terrain):
    # The PNG is only a seed. Erosion and shoreline locking change its water
    # intersection substantially (the seed puts a false coast beside the home).
    # Read the same processed grid the engine loads, then apply its saved sculpt.
    directory = ROOT/'binaries/project/plan_resources'
    cache = directory/'terrain_cache.bin'
    if not cache.exists():
        raise FileNotFoundError('Load plan in the engine to generate terrain_cache.bin before building soundscapes; the raw heightmap is not a coastline.')
    with cache.open('rb') as f:
        cache_hash, width, rows, heights, count, dense_width, dense_height, _ = struct.unpack('<Q6If', f.read(36))
        density = int(terrain.get('density','1'))
        assert (width, rows) == (projection.width_px, projection.height_px), 'Terrain cache dimensions differ from the atlas'
        assert (dense_width, dense_height) == (density*(width-1)+1, density*(rows-1)+1), 'Regenerate the terrain cache after changing density'
        assert heights == count == dense_width*dense_height
        assert cache.stat().st_size == 36 + heights*4 + count*12, 'Truncated terrain cache'
        f.seek(36 + heights*4)
        xyz = np.fromfile(f, dtype='<f4', count=count*3).reshape(dense_height,dense_width,3)
    x = xyz[0,:,0].astype(float)
    z = xyz[:,0,2].astype(float)
    step = projection.meters/density
    assert np.allclose(x, np.arange(dense_width)*step-projection.half_x, atol=.01)
    assert np.allclose(z, np.arange(dense_height)*step-projection.half_z, atol=.01)
    height = xyz[:,:,1].astype(float)
    sculpt = directory/'terrain_sculpt.bin'
    if sculpt.exists():
        with sculpt.open('rb') as f:
            magic, version, ox, oz, cx, cz, cells, tiles = struct.unpack('<II4fII',f.read(32))
            assert magic == 0x4c435053 and version == 1 and cells == 64, 'Unsupported terrain sculpt format'
            assert np.allclose([ox,oz,cx,cz], [x[0],z[0],step,step], atol=.01), 'Sculpt and terrain grids differ'
            for _ in range(tiles):
                tx,tz = struct.unpack('<ii',f.read(8))
                tile = np.frombuffer(f.read(cells*cells*4),dtype='<f4').reshape(cells,cells)
                x0,z0 = tx*cells,tz*cells
                left,bottom = max(x0,0),max(z0,0)
                right,top = min(x0+cells,dense_width),min(z0+cells,dense_height)
                if left < right and bottom < top:
                    height[bottom:top,left:right] += tile[bottom-z0:top-z0,left-x0:right-x0]
            assert not f.read(1), 'Unexpected terrain sculpt payload'
    height += projection.terrain_y
    assert np.isfinite(height).all()
    return x,z,height,dict(cache_hash=str(cache_hash),cache_sha256=hashlib.sha256(cache.read_bytes()).hexdigest(),sculpt_sha256=hashlib.sha256(sculpt.read_bytes()).hexdigest() if sculpt.exists() else None)


def coastal_land(x, z, height, sea_level, simplify=18):
    # Only water connected to the grid's ocean border can create a surf edge.
    # Flood filling excludes landlocked depressions, even below sea level.
    land_mask = binary_fill_holes(height > sea_level)
    surface = np.where(land_mask, np.maximum(height, sea_level+.01), height)
    contours = plt.contour(x, z, surface, levels=[sea_level])
    polygons = []
    for ring in contours.allsegs[0]:
        if len(ring) < 4: continue
        assert np.allclose(ring[0],ring[-1]), 'Coast reaches the terrain grid edge; do not invent a closing surf segment'
        polygons.append(Polygon(ring).buffer(0))
    plt.close()
    assert polygons, 'No ocean coastline in the processed terrain'
    land = unary_union(polygons).buffer(0).simplify(simplify, preserve_topology=True)
    return unary_union([Polygon(p.exterior) for p in parts(land)])


def geometry(projection, terrain):
    x,z,height,provenance = terrain_grid(projection,terrain)
    land = coastal_land(x,z,height,float(terrain.get('level_sea','0')))
    seeds = [np.array(projection.lonlat_to_world(r[2],r[3])) for r in REGIONS]
    districts = []
    for i, a in enumerate(seeds):
        cell = land
        for j, b in enumerate(seeds):
            if i != j: cell = cell.intersection(half_plane(a,b))
        districts.append(cell)
    return land, districts, provenance

def build(apply):
    OUT.mkdir(parents=True,exist_ok=True)
    text = WORLD.read_text(encoding='utf-8')
    root = ET.fromstring(text)
    terrain = root.find('.//terrain')
    atlas = json.loads((ROOT/'binaries/project/maps/plan_map.json').read_text())
    projection = Projection(atlas['crs'])
    terrain_entity = next(e for e in root.iter('Entity') if e.find('terrain') is terrain)
    projection.terrain_y = float(terrain_entity.get('position').split()[1])
    assert terrain_entity.get('rotation') == '0 0 0 1' and terrain_entity.get('scale') == '1 1 1'
    assert [float(v) for v in terrain_entity.get('position').split()][::2] == [0,0], 'Atlas assumes a centered terrain'
    assert float(terrain.get('scale')) == projection.meters
    land, districts, terrain_provenance = geometry(projection,terrain)
    sounds, records = load_sources()
    files = []
    for index, r in enumerate(REGIONS):
        frames = RATE * (47 + index*2)
        mix = np.zeros((frames,2))
        for key, gain in r[4].items():
            mix += tile_loop(sounds[key][0],frames, index*RATE*3)*gain
        files.append(write_loop(OUT/(r[0]+'.wav'), mix))
    surf = np.concatenate(sounds['surf'])
    files.append(write_loop(OUT/'shore.wav',tile_loop(surf,RATE*53)*.65))
    files.append(write_loop(OUT/'harbour.wav',tile_loop(sounds['harbour'][0],RATE*59)*.40))

    holder = ET.Element('Entity',name='Island Soundscapes',id='9026000000000000000',active='true',position='0 0 0',rotation='0 0 0 1',scale='1 1 1',tags='island_soundscapes')
    manifest = dict(description='Eight authored acoustic districts, separate processed-terrain ocean shoreline. Boundaries are sound design, not administrative borders.', projection=atlas['crs'], terrain=terrain_provenance, sources=records, audio=files, regions=[], shoreline=[])
    next_id = 9026000000000000001
    def add_region(shape, name, clip, group, fade, boundary=False):
        nonlocal next_id
        for part in parts(shape):
            if part.area < 1000: continue
            # Shared borders retain identical vertices; individual simplification would open gaps.
            outline = [[round(x,2),round(z,2)] for x,z in list(part.exterior.coords)[:-1]]
            e = ET.SubElement(holder,'Entity',name=name,id=str(next_id),active='true',position='0 0 0',rotation='0 0 0 1',scale='1 1 1',tags='soundscape_region')
            next_id += 1
            x0,z0,x1,z1 = part.bounds
            volume = ET.SubElement(e,'volume',bb_min_x=str(x0),bb_min_y='-12',bb_min_z=str(z0),bb_max_x=str(x1),bb_max_y='90' if boundary else '1000',bb_max_z=str(z1),audio_fade_distance=str(fade),audio_boundary_only=str(boundary).lower(),audio_group=group,reverb_enabled='false')
            polygon = ET.SubElement(volume,'AudioPolygon')
            for x,z in outline: ET.SubElement(polygon,'Point',x=str(x),z=str(z))
            ET.SubElement(e,'audio_source',path=f'project/soundscapes/{clip}.wav',ambient='true',is_3d='false',loop='true',play_on_start='true',volume='0.55' if boundary else '0.48',pitch='1',reverb_enabled='false')
            probe = part.representative_point()
            manifest['shoreline' if boundary else 'regions'].append(dict(id=e.get('id'),name=name,clip=clip,polygon=outline,fade=fade,area=part.area,test_position=[probe.x,30,probe.y]))
    for r, district in zip(REGIONS,districts): add_region(district,r[1],r[0],'island_bed',180)
    add_region(land,'Coastal surf','shore','island_shore',180,True)
    port = next(p for p in atlas['pins'] if p['id']=='port')
    add_region(Point(port['x'],port['z']).buffer(420,quad_segs=12),'Zakynthos harbour detail','harbour','island_detail',160)
    ET.indent(holder,space=' ')
    block = ET.tostring(holder,encoding='unicode')
    begin=' <!-- ISLAND SOUNDSCAPES BEGIN -->'
    end=' <!-- ISLAND SOUNDSCAPES END -->'
    if begin in text:
        before, rest = text.split(begin,1)
        _, after = rest.split(end,1)
        updated = before+begin+'\n'+block+'\n'+end+after
    else:
        # Engine saves discard comments. Replace the tagged hierarchy in that
        # case too, without reserializing unrelated entities or duplicating IDs.
        match = re.search(r'<Entity\b[^>]*\btags="[^"]*\bisland_soundscapes\b[^"]*"[^>]*>', text)
        if match:
            depth = 0
            for tag in re.finditer(r'</?Entity\b[^>]*>', text[match.start():]):
                token = tag.group()
                if token.startswith('</'): depth -= 1
                elif not token.endswith('/>'): depth += 1
                if depth == 0:
                    text = text[:match.start()] + text[match.start()+tag.end():]
                    break
        assert text.count('</Entities>')==1
        updated = text.replace('</Entities>',begin+'\n'+block+'\n'+end+'\n </Entities>')
    # Prove the operation leaves all existing entities and components intact.
    updated_root=ET.fromstring(updated)
    original={e.get('id'):ET.tostring(e) for e in root.find('Entities') if 'island_soundscapes' not in e.get('tags','')}
    current={e.get('id'):ET.tostring(e) for e in updated_root.find('Entities') if 'island_soundscapes' not in e.get('tags','')}
    assert {k:v.strip() for k,v in original.items()} == {k:v.strip() for k,v in current.items()}
    ids=[e.get('id') for e in updated_root.iter('Entity')]
    assert len(ids)==len(set(ids))
    # Representative geographic coverage and exact partition coverage.
    assert land.symmetric_difference(unary_union(districts)).area < 1
    manifest['land_area_km2']=land.area/1e6
    manifest['locations']=[dict(name=p['name'],x=p['x'],z=p['z'],region=REGIONS[min(range(len(REGIONS)),key=lambda i: districts[i].distance(Point(p['x'],p['z'])))][1]) for p in atlas['pins']]
    (OUT/'regions.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (OUT/'soundscapes.world').write_text('<?xml version="1.0"?><World name="soundscape_test"><Entities>'+block+'</Entities></World>',encoding='utf-8')
    (OUT/'SOURCES.md').write_text('# Soundscape recordings\n\nAll source recordings are CC0-1.0: https://creativecommons.org/publicdomain/zero/1.0/\n\nThese are adapted reference recordings, not recordings made on Zakynthos. Freesound inputs use publicly available HQ MP3 previews.\n\n'+ '\n'.join(f'- {s["name"]}: {s["author"]} — {s["page"]}' for s in records)+'\n\nDerived stereo PCM loops: high-pass filtering, resampling, level matching, artistic mixing, and crossfaded loop joins. Rebuild with tools/map/build_soundscapes.py.\n',encoding='utf-8')
    if apply:
        if any('world_layer' in e.get('tags','').split(',') for e in root.find('Entities')):
            from organize_world import organize
            updated,_=organize(updated)
        backup=ROOT/'binaries/project/backups/plan_before_soundscapes.world'
        if not backup.exists(): shutil.copy2(WORLD,backup)
        WORLD.write_text(updated,encoding='utf-8',newline='\n')
    print(json.dumps(dict(applied=apply,acoustic_districts=len(REGIONS),volumes=len(holder),land_area_km2=round(land.area/1e6,2),audio_files=len(files),audio_mb=round(sum((OUT/f['file']).stat().st_size for f in files)/1e6,2)),indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--apply',action='store_true');build(parser.parse_args().apply)
