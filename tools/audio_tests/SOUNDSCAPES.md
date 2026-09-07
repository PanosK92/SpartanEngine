# Zakynthos soundscapes

`worlds/plan.world` contains a **Soundscapes** hierarchy with eight
acoustic districts, a separate coast-following surf layer, and a small harbour
detail volume. These are editable sound-design boundaries, not administrative
districts or permanent gameplay zones. Existing roads, landmarks, terrain,
traffic, and the player's home are unchanged.

The eight districts are Vrachionas and the western villages; Volimes and Skinari;
Alykes, Tragaki and Tsilivi; the central olive plain; Laganas, Kalamaki and the
airport plain; Skopos and Vasilikos; Keri and the southwest; Zakynthos Town and
Bochali. Their individual mixes use different proportions of wind, cicadas,
birds and rural atmosphere. Harbour activity is confined to the port rather
than spread across the entire town district. This is a summer daytime/dusk
palette; it does not yet switch to separate nighttime or weather recordings.

## World hierarchy

The root has eight layers: **Landscape**, **Roads**, **Places**, **Population**,
**Player**, **Lighting**, **Soundscapes**, and **Development**. Places retain
their existing named town/village subtrees. Player contains the home, car, and
camera rig. Soundscapes contains Regional Beds (one folder per district),
Shoreline, and Local Details. Development retains the older playground and
loose demonstration props; nothing is deleted or disabled.

`python tools/map/organize_world.py --apply` restores this organization after
content changes. It uses identity parents and verifies every original world
transform, entity ID, component, and spline control-point order. Repeated runs
are identical. The soundscape and location builders retain these layers.
Individual spline points stay directly beneath their owning spline because
their order is functional data.

## Geography

The builder uses the existing `plan_map.json` longitude/latitude transform,
including the map's compressed north/south scale. Districts are a nearest-anchor
partition clipped to the land. They are approximate acoustic coverage, not a
survey of vegetation. The processed terrain cache, with saved sculpt edits and
the terrain's -5.9 m offset, defines the shoreline. The original heightmap is
only a seed: using its contour incorrectly put surf beside the inland home.
Water must connect to the ocean at the grid boundary to create a coastline;
landlocked depressions are excluded. Surf depends on horizontal distance to
that coastline and is silent beyond 180 m, regardless of how low the land is.
Disconnected pieces and small islands have their own footprints but share
audio clips and mix groups.

References: the existing atlas and named location pins, and the island's
[geography and nature guide](https://sunflowerbooks.co.uk/ebook-sample/Sunflower-Zakynthos-sample.pdf).
Western high ground is windier in this authored mix, while the lower central
and southeastern regions feature more insects and birds. Recordings are
adapted references, not field recordings made on Zakynthos.

## Playback and editing

Use the rebuilt development engine, load **plan**, and enter play mode.
Ambience is silent in edit mode. Stop the car engine to hear the quiet layers.
Each source uses an ordinary **Audio Source** plus a **Volume** on the same entity:

- Enable **Volume Ambience** to use listener-driven, stereo region playback.
- **Volume** sets the layer level; **Mute** disables it with a fade.
- **Blend Distance** feathers boundaries in local meters (180 m on the island).
- **Mix Group** caps overlapping region gains to prevent volume buildup.
- **Shoreline Only** plays near a polygon's edge on both sides, rather than
  throughout its interior. It also fades with height above the volume bounds.
- The footprint is stored as local X/Z `AudioPolygon/Point` coordinates in the
  Volume XML; a volume with no polygon uses its box. Entity transforms apply.
  Polygon bounds must enclose its vertices. Regenerate districts by editing
  `REGIONS` in `tools/map/build_soundscapes.py`.
- Console `audio.ambience_volume 0.5` halves all ambience without changing cars,
  music, or other sounds. `0` fades it out, `1` restores the authored levels.

SDL's existing shared device mixes the streams; no middleware or synthesizer
is required. Stereo source channels are preserved for ambience; ordinary
positional audio retains its existing mono-to-stereo treatment. Spatial weights
refresh at 10 Hz and the sample-level gain follows a 0.5-second envelope.
Inactive streams retire after fading and resume at a virtual playback position.
Existing reverb volumes reduce outdoor ambience to 20% around the listener;
that is an indoor approximation, not acoustic occlusion through arbitrary walls.

## Assets and reproduction

Ten PCM stereo WAV loops live in `binaries/project/soundscapes` (about 67 MB).
They are ready for local playback. As with other project assets, `binaries/` is
Git-ignored. A clean checkout needs the existing terrain/map assets. Load plan
in the engine first to generate its processed terrain cache, then use the
following preparation; the downloaded WAVs are not automatically uploaded:

```
python -m pip install numpy scipy matplotlib pillow shapely soundfile
python tools/audio_tests/fetch_ambience.py
python tools/map/build_soundscapes.py --apply
```

Source URLs, authors, CC0 status, and SHA-256 hashes are checked into
`tools/audio_tests/ambience_sources.json`. Downloads are cached; known hashes
are checked. Freesound inputs use its publicly available HQ MP3 previews;
OpenGameArt inputs use WAV/FLAC downloads. No account credentials are needed.
The preparation resamples to 32 kHz stereo, removes sub-bass, matches source
levels, mixes district beds, and crossfades recording tails into heads.
`SOURCES.md` and `regions.json` alongside the WAVs retain provenance and the
authored district geometry. Regeneration backs up the original world under
`binaries/project/backups/plan_before_soundscapes.world`.
Regenerate soundscapes after coastline-changing terrain edits; cache and sculpt
hashes are recorded in `regions.json`. The builder refuses to substitute the raw
heightmap when the processed cache is absent.

## Checks

`tools/audio_tests/run_regions.cmd` checks production polygon distance,
concave footprints, winding, shoreline rejection, complementary boundary
weights, and envelope timing. Build `spartan.vcxproj` in `development`.

`python -m unittest discover -s tools/map -p test_soundscape_coast.py` checks
ocean connectivity, inland depressions, and the actual home/airport against the
authored surf polygons. It writes probes for `node tools/audio_tests/live_coast.mjs`,
which checks runtime surf silence inland at both sea level and 30 m altitude,
plus playback within 50 m and silence 250 m from the real coast on either side.
Use an empty disposable engine on port 47785 for this check too.

`node tools/audio_tests/live_ambience.mjs` uses a disposable empty engine on port
47785 (`--mcp-control --mcp-port=47785`) with the repository `binaries` working
directory. It checks actual component loading, regional playback, distant voice
retirement, pause/resume, movement out of coverage, and world save. It never
loads or saves the user's active island. Its fixtures are in the ignored
soundscapes asset directory. After that check, `node tools/audio_tests/check_island.mjs`
loads the actual island in the same disposable engine and checks ambience at
the player's home, without saving the island. Source licensing is CC0-1.0;
engine code remains MIT.
