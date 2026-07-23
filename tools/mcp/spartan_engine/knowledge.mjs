export const component_property_catalog = {
  render: {
    properties: ["mesh", "material", "default_material", "visible", "casts_shadows", "exclude_from_ray_tracing", "max_render_distance", "max_shadow_distance"],
    notes: "controls primitive mesh, material, visibility, shadowing, and render distance; use render_set_mesh for cached, imported, or generated mesh resources",
  },
  physics: {
    properties: ["body_type", "static", "kinematic", "enabled", "mass", "friction", "friction_rolling", "restitution", "center_of_mass", "linear_velocity", "angular_velocity"],
    notes: "controls rigid body type, mass properties, velocities, and physics state",
  },
  light: {
    properties: ["light_type", "color", "temperature", "intensity", "range", "angle_degrees", "area_width", "area_height", "shadows", "volumetric", "draw_distance", "shadow_distance", "volumetric_distance"],
    notes: "intensity is lux for directional and lumens otherwise; visible blockout defaults are point/spot 8500, area 12000, directional 120000; prefer entity_create_light so type, intensity, range, angle, and area size are all initialized",
  },
  camera: {
    properties: ["fov_degrees", "aperture", "shutter_speed", "iso", "projection", "controllable", "flashlight"],
    notes: "controls projection, exposure values, field of view, and editor control flags",
  },
  audio_source: {
    properties: ["clip", "mute", "play_on_start", "loop", "is_3d", "volume", "pitch", "reverb_enabled", "reverb_room_size", "reverb_decay", "reverb_wet"],
    notes: "controls the audio clip, playback flags, 3d mode, gain, pitch, and reverb",
  },
  script: {
    properties: ["file_path"],
    notes: "loads a lua script file onto the entity",
  },
  spline: {
    properties: ["closed_loop", "resolution", "road_width", "mesh_enabled", "profile", "height", "thickness", "tube_sides", "road_width_end", "uv_tiling_u", "uv_tiling_v", "sidewalk_enabled", "sidewalk_width", "curb_height", "conform_to_terrain", "terrain_offset", "source_spline_entity_id", "attach_mode", "attach_lateral_offset", "attach_vertical_offset", "attach_inherit_closed_loop", "attach_sample_count", "instance_spacing", "align_instances_to_spline", "instance_mesh_path", "instance_template_id", "instance_lateral_offset", "instance_mirror", "instance_face_inward", "instance_random_offset", "instance_random_scale_min", "instance_random_scale_max", "instance_random_yaw"],
    notes: "controls spline shape, generated mesh settings, source attachment, and instanced placement",
  },
  spline_follower: {
    properties: ["spline_entity_id", "speed", "follow_mode", "align_to_spline", "flip_forward", "progress"],
    notes: "controls runtime movement along a spline, use spline_query on the spline entity for length, travel time, and closest point math, set flip_forward when the mesh faces backwards while driving",
  },
  terrain: {
    properties: ["min_y", "max_y", "level_sea", "level_snow", "smoothing", "density", "scale", "create_border", "width", "height", "area_km2", "height_samples", "vertex_count", "index_count", "triangle_count"],
    notes: "controls terrain generation parameters and exposes generated terrain statistics",
  },
  volume: {
    properties: ["bounding_box", "reverb_enabled"],
    notes: "controls volume bounds and audio reverb behavior",
  },
  particle_system: {
    properties: ["preset", "max_particles", "emission_rate", "lifetime", "start_speed", "start_size", "end_size", "start_color", "end_color", "gravity_modifier", "emission_radius", "emission_direction", "emission_cone_angle", "directional_blend", "blend_mode", "lighting_mode", "emissive_strength", "soft_depth_scale", "drag", "turbulence_strength", "wind_influence", "velocity_inheritance", "velocity_stretch", "spawn_burst", "flipbook_rows", "flipbook_columns", "flipbook_fps", "effect_path"],
    notes: "controls emitter simulation, colors, motion, rendering, bursts, and flipbook playback",
  },
  skid_marks: {
    properties: ["slip_threshold", "min_segment_distance", "max_segments", "opacity", "z_offset", "uv_tiling", "fade_distance", "center_smoothing"],
    notes: "controls vehicle skid mark generation and rendering",
  },
};

export const engine_overview = [
  "# Spartan Engine MCP",
  "",
  "Spartan exposes a live editor and runtime control surface through a local C++ TCP bridge on 127.0.0.1:47777.",
  "The Node MCP adapter exposes stable tools over stdio for external agents and forwards commands to the engine bridge.",
  "Bridge commands carry request ids echoed in responses and debug logs.",
  "The in-editor assistant talks to assistant.mjs on 127.0.0.1:47778 and uses deterministic fast paths before escalating to Cursor.",
  "",
  "Prefer context_snapshot, camera_snapshot, world_raycast, entity_resolve, component_get, and native batch tools before issuing many small commands.",
  "For explicit greyboxes, resolve or create the parent with entity_resolve/entity_create_empty, then build with entity_create_primitive_batch and entity_create_light. For normal scene-construction requests, treat the result as finished procedural content with generated geometry, semantic materials, descriptive feature names, and calibrated lighting.",
  "Do not use execute_lua for API discovery or pairs/next probing; that path has crashed the engine. Prefer native batch tools, and only use one focused Lua script with known bindings when a batch tool cannot express the edit.",
  "Use entity_find_by_component to locate all entities with a given component type without listing the whole scene.",
  "Read component_get property_metadata and member_metadata for ranges, units, enum values, side effects, recommended defaults, and read-only reasons before unfamiliar component writes.",
  "Use component_set_batch when changing multiple properties on the same component.",
  "Use entity_set_transform_batch when repositioning many entities; one call replaces a run of entity_set_transform calls.",
  "For procedural scene edits, execute_lua is available but secondary to native batch tools; it is edit-mode guarded and must not be used for exploratory API probing.",
  "Lua entity lists are tables: World.GetEntities(), World.GetEntitiesLights(), and entity:GetChildren() return 1-based Lua tables; prefer entity:ForEachChild for iteration.",
  "Lua resolves entities with World.GetEntityByName(name) for exact names or World.GetEntityById(id) with the id as a string.",
  "Use component_action for deterministic component methods such as terrain generation, spline mesh/instance operations, particle presets/bursts, physics forces, audio playback, light fitting, and camera focus.",
  "Use resource_list and material_get to inspect cached assets and material state; use material_set_property and material_set_texture for material edits.",
  "Use resource_load, resource_reload, resource_save, resource_remove, and material_create for asset cache lifecycle work.",
  "world_save automatically prunes unreferenced mesh, material, and texture files from the loaded world's managed resources directory. Use world_resources_clean for an explicit cleanup receipt.",
  "For every finished environment, call scene_plan_create before geometry. Use a request-specific scale reference, purposeful zones, realistic expected dimensions, support modes, relationships, and lighting intent; this workflow is generic and must not impose one environment domain on another.",
  "After construction, call scene_layout_audit to validate physical scale, zone containment, support gaps, declared relationships, calibrated light values, shadows, and scene coverage.",
  "Use construction_grammar_create for reusable medium-scale assemblies: window grids, doors, gable or flat roofs, stairs, railings, structural frames, panel walls, support arrays, signs, cable runs, and facades. Size and material parameters keep these grammars environment-agnostic.",
  "Use mesh_generate for bounded parametric shapes, mesh_generate_batch for several reusable shapes, render_set_mesh for cached or imported meshes, and compound_create for editable multi-part furniture and props.",
  "Before detailed scene construction, call material_palette_create and assign its semantic material names to compound parts. Default themes are cozy, neutral, cool, and vibrant.",
  "Use detail_pattern_create for keyboard keys, drawers, slats, books, buttons, cables, cushions, and wall trim instead of hand-authoring repeated entities.",
  "After creating an object, call entity_snap to place it against a floor, ceiling, wall, or arbitrary static surface using its rendered bounds.",
  "Finish scene edits with scene_quality_audit and scene_visual_review. Fix every deterministic failure, inspect the PNG, apply at least one targeted material, transform, geometry, composition, or lighting correction, then audit again. Do not report completion while the audit pass is false.",
  "Read spartan://engine/parametric-modeling before generating detailed furniture; it defines dimensions, budgets, shape selection, and reusable patterns.",
  "Use undo_redo for editor command-stack undo and redo.",
  "Use viewport_frame to frame a complete hierarchy from perspective, front, back, left, right, or top views. It computes descendant render bounds and does not use keyboard focus. Use camera_set_view only for a deliberate custom pose.",
  "Use profiler_snapshot to read frame timing, fps, stutter flags, rhi counters, and per-pass CPU/GPU time blocks; sort by duration and inspect the top blocks to find optimization targets.",
  "Use selection_update, entity_clone, entity_move_index, prefab_types, prefab_save, and prefab_load before Lua for common editor hierarchy and prefab workflows.",
  "Use scene_visual_review for visual verification. It captures perspective and top views by default; add a front or side view when checking facades, support, or alignment.",
  "Before destructive rebuilds that should preserve appearance, call entity_render_materials and reuse the returned material names on new render geometry.",
  "Use entity_create_light for every light; it fully initializes type, color, temperature, intensity, range, angle, area size, shadows, and distances. Never hand-roll lights with empty + add component + component_set.",
  "Every renderable receives static collision by default. Standard primitives use matching convex compatible colliders; generated and imported meshes use mesh_convex. Collision coverage must reach 100 percent before scene completion.",
  "Light intensity is lux for directional and lumens otherwise. Visible blockout defaults: point/spot 8500, area 12000, directional 120000. Values like 25-100 are invisible and get replaced by calibrated defaults.",
  "Use lights_calibrate to fix existing scene lights in one call; it keeps specialty car lights dim and lifts weak blockout lights.",
  "For city development: massing first with city_blockout / district_blockout, roads second. Never hand-place hundreds of cubes for a city.",
  "district_blockout presets: market, downtown/skyscrapers, park, industrial, residential, parking, plaza, gas_station. city_blockout places several districts with corridor gaps and avoid_existing landmarks, returning road_hints.",
  "Road pass: world_landmarks, invent an arterial that skirts large districts, spur to edges, spline_junction, spline_decorate. Never triangle center-to-center through an airway.",
  "Use spline_reroute to fix an existing road that cuts through buildings or other roads; it keeps lights/cameras and redistributes them on the new path.",
  "Never drive through an airway/runway or yard footprint. spline_connect approaches landmark edges and skirts obstacles; use via points when the arterial must go around a district.",
  "spline_decorate adds sidewalks, street lights, and roadside props. Bare center-to-center lines are wrong.",
  "Use debug_log_read or spartan://agent/debug-log to inspect assistant prompts, engine command arguments, durations, and outputs.",
  "Use async_task_start and async_task_get for long-running MCP tools that should be reported without blocking the main client request.",
  "Live scene edits should route through deterministic tools when one matches, and fall back to the Cursor agent with the engine MCP tools otherwise.",
  "Treat scene construction prompts such as build a level, make rooms, backrooms, and liminal space as live scene edits, not source-code search requests.",
  "Missing deterministic capabilities should be recorded in agent memory under Advice To Maintainers immediately.",
  "Splines are scriptable from Lua: entity:GetComponent(ComponentType.Spline) exposes GetPoint(t), GetTangent(t), GetLength(), GetTAtDistance(distance), GetControlPointCount(), GetClosedLoop(), and GetRoadWidth(); GetPoint returns world-space positions with t in [0, 1], ideal for placing entities along roads.",
  "Cameras are scriptable from Lua: entity:AddComponent(ComponentType.Camera) adds a camera and the returned component exposes GetFovHorizontalDeg and SetFovHorizontalDeg.",
  "The editor sequencer has two tracks: a camera cut track controlled through sequencer_get, sequencer_set, sequencer_playback, sequencer_event_add, sequencer_event_update, and sequencer_event_remove, and a spline follower track controlled through sequencer_spline_add, sequencer_spline_update, and sequencer_spline_remove; each camera event cuts to a camera at its time and stays active until the next event, and edits auto-save next to the loaded world.",
  "Sequencer camera events optionally carry a lock target: pass target on sequencer_event_add or sequencer_event_update and the camera pans to keep that entity in view while the event is active.",
  "Cameras can ride on moving entities for on-board shots: entity_create_empty with parent_id (e.g. the spline car root), entity_set_transform for a local offset (e.g. low beside a wheel arch), entity_add_component camera, then reference it in sequencer events; with the moving entity as the lock target the framing stays rigid on the body, and a fresh camera component already has sensible cinematic defaults (90 degree fov, f/5.6, 1/125s, iso 200).",
  "Entities created under a prefab instance (like a car) count as user additions and are saved in full by world_save, so on-board cameras and similar attachments persist; parent to the car root, not a wheel, because wheels are transient runtime entities and spin.",
  "Sequencer spline events are windows on the second track: each has start_time, end_time and a follower entity (SplineFollower component); while the sequencer plays or scrubs, each spline event drives its follower along the spline by the timeline time (position = speed * time, clamped to the window edges) so the entity moves only inside its window; without a spline event a follower is not driven by the sequencer.",
  "Use spline_query for camera cut math: call it with no arguments and it auto-picks the followed spline, projects every camera in the world onto it, and returns per camera arc_distance and pass_time_seconds (the exact moment the follower passes that camera); all cut times come from one call, never sample the spline yourself.",
  "Use spline_distribute to spread entities evenly along a spline: with no arguments it takes every camera child of the spline entity and respaces them by arc length, preserving their order, lateral offset, and framing; one call replaces any manual placement math or Lua.",
  "spline_distribute also repositions relative to the road: edge_offset is signed meters beyond the road edge (positive = right of travel, tracks varying road width so entities always clear the asphalt), lateral_offset is signed meters from the centerline, height is meters above the road surface; e.g. edge_offset 2 with height 2 puts all cameras just off the right side of the road so the car never drives through them.",
  "spline_follower is fully editable through component_get and component_set: spline_entity_id (entity id), speed (meters per second), follow_mode (clamp, loop, ping_pong), align_to_spline, flip_forward (true when the mesh drives backwards, rotates it 180 degrees), and progress (normalized 0 to 1).",
  "spline_follower also animates car wheels through component_set: animate_wheels (bool) rolls every wheel with the travelled distance and steers the front wheels into the turns; it finds wheels through entity tags (wheel marks a wheel, wheel_front marks a steering wheel); wheel_radius (meters, 0 auto estimates from the wheel mesh bounds) controls roll speed and max_steer_angle (degrees) caps the front wheel steering; this runs while the sequencer plays or scrubs, no physics needed.",
  "",
  "Cars are defined by .car xml files (worlds/cars/*.car), one file per car: a body node (model path, scale, forward_z nose direction, hide_parts baked tires to hide), a wheels node (wheel model and textures), and the simulation tuning split into thematic sections (chassis, engine, transmission, drivetrain, brakes, suspension, steering, tires, aero, assists, input, simulation, upgrades); the loader reads tuning attributes from any section, so grouping is purely for readability; the car prefab in a world references one via its file attribute, resolved relative to the world file.",
  "Spawning a car from a .car file is deterministic: a root entity tagged car, a body child tagged body, and four wheel entities named wheel_front_left etc, each tagged wheel plus wheel_front/wheel_rear and wheel_left/wheel_right; prop (non drivable) cars get the same hierarchy with wheels placed where the baked tires were, so spline_follower wheel animation works out of the box.",
  "Entities carry free-form tags: entity_find filters by tag, entity_update sets tags (comma separated, replaces all), and entity payloads include a tags array when present; use tags to find parts by role (wheel, wheel_front) instead of guessing names.",
  "Drivable cars are controllable through MCP in play mode: vehicle_list / vehicle_get, vehicle_enter (same as E, enables chase cam and mcp pedal ownership), vehicle_set_input (throttle/brake 0-1, steering -1..1, handbrake 0-1), vehicle_shift, vehicle_set_view (chase/hood/wheel/cycle), vehicle_reset (R), vehicle_exit, and vehicle_telemetry.",
  "Human car controls for reference: Arrow Up gas, Arrow Down brake, Left/Right steer, Space handbrake, E enter/exit, V cycle view, C recenter chase cam, R reset, F3 telemetry hud, L headlights, L1/R1 shift. WASD is flight/walk camera when not in the car.",
  "vehicle_set_input marks the car mcp_controlled so keyboard zeros do not overwrite agent pedals; vehicle_exit clears that flag. Prefer engine_set_mode play, vehicle_enter, hold inputs for a few seconds via repeated set_input or async tasks, then vehicle_telemetry.",
  "Telemetry is car_telemetry.csv in the engine working directory (usually the binary folder), not Excel. vehicle_telemetry returns the absolute path plus the csv header and the last max_rows rows for diagnosing spin-out, grip, suspension, thermals, and input issues. Agents can read telemetry and report bugs; they cannot compile the engine.",
].join("\n");

export const scene_planning_guide = [
  "# Spartan Generic Scene Planning",
  "",
  "Use this workflow for every environment type. Do not embed assumptions from rooms, roads, vehicles, cities, or any other specific domain into unrelated requests.",
  "",
  "## Plan before geometry",
  "",
  "Call scene_plan_create before creating scene entities. Infer one credible scale reference from the requested environment and express its full width, height, and depth in meters.",
  "Divide the environment into purposeful zones. Zones describe composition and use, not engine-specific geometry.",
  "List semantic elements with descriptive names, roles, realistic expected dimensions, support modes, clearances, materials, and zones.",
  "Expected sizes use world x,y,z dimensions by default so incorrect orientation is detectable. Set allow_axis_permutation only when orientation is intentionally unconstrained.",
  "Declare important spatial relationships explicitly. Use on, inside, connected_to, separated_from, aligned_with, and beside only when the relationship materially affects whether the layout makes sense.",
  "Define a lighting intent with at least one purposeful calibrated light. Natural environments can use a directional light; local environments should use appropriately ranged point, spot, or area lights.",
  "",
  "## Build from semantics",
  "",
  "Entity or compound names must match planned semantic element names. Repeated parts use the semantic name as a prefix.",
  "Use entity_snap for grounded or surface-mounted elements. Suspended elements must be visibly connected to a support in the plan.",
  "Expected dimensions are constraints, not decorative suggestions. Preserve believable proportions relative to the scale reference.",
  "Use generated meshes and detail patterns where silhouette or function benefits, while keeping broad surfaces and structural elements economical.",
  "Use construction grammars between primary massing and tertiary detail. They supply repeated openings, frames, roofs, circulation structures, supports, signage, and service runs without domain-specific scene code.",
  "",
  "## Validate and correct",
  "",
  "Call scene_layout_audit after construction. Fix every failed scale, zone, support, relationship, and lighting check.",
  "Call scene_quality_audit for material coverage, geometric richness, requested features, and detail density.",
  "Call scene_visual_review after deterministic checks. Inspect both perspective and top views for composition, repetition, readability, exposure, intersections, traffic or circulation logic, and empty areas, then make at least one targeted correction.",
  "Run both audits again. Completion requires both pass values to be true.",
].join("\n");

export const construction_grammar_guide = [
  "# Spartan Construction Grammars",
  "",
  "Construction grammars create editable medium-scale assemblies between primary massing and small detail patterns. They are generic building blocks, not environment presets.",
  "",
  "## Common rules",
  "",
  "Size is full local width, height, and depth in meters. X is width, y is height, and z is depth.",
  "Position and rotation apply to the grammar root. Parts use local transforms below that root.",
  "Use snap_mode floor, wall, ceiling, or surface when the assembly should be placed against existing static geometry. Use snap_offset only for intentional separation.",
  "Assign primary_material for the main structure, secondary_material for panels or infill, accent_material for trim and hardware, glass_material for glazing, and emissive_material for illuminated sign faces.",
  "Use repeated instance root names from the scene plan, then place the grammar below that semantic instance.",
  "Keep broad massing separate. A facade grammar belongs on a building mass; a railing belongs on a balcony, stair, platform, or boundary.",
  "",
  "## Grammar selection",
  "",
  "window_grid creates shared frames and individually editable panes.",
  "doorway creates jambs, header, inset door leaf, handle, and threshold.",
  "gable_roof creates two pitched slabs, ridge cap, and fascia inside the requested envelope. Omit pitch_degrees to fit the roof to size; an explicit pitch must agree with the requested height.",
  "flat_roof creates a slab and four parapets inside the requested envelope.",
  "stairs creates stepped treads and two wedge stringers.",
  "railing creates repeated posts plus mid and top rails along the longest horizontal axis.",
  "structural_frame creates columns, longitudinal beams, and cross beams.",
  "panel_wall creates a grid of inset panels with controlled gaps.",
  "support_array creates evenly distributed square or round supports.",
  "sign creates posts, framed panel, and optional emissive face.",
  "cable_run creates one swept pipe and clips at path points; path points are local to the grammar root.",
  "facade creates a readable grid of glazed and solid bays with shared vertical and horizontal trim.",
  "room_shell creates an open-front floor, ceiling, and three-wall shell without duplicate enclosing cubes.",
  "storefront_bay creates display glazing, an entrance, framing, a sign band, and a canopy as one coordinated bay.",
  "warehouse_bay creates a framed loading bay with wall infill, a sectional door, and visible door slats.",
  "boarding_gate creates a jetbridge, terminal portal, supports, glazing, and an illuminated gate sign.",
  "",
  "Use construction_grammar_create repeatedly with different dimensions and material roles to create variation. Follow with detail_pattern_create only where close-view detail is useful.",
].join("\n");

export const parametric_modeling_guide = [
  "# Spartan Parametric Modeling",
  "",
  "## Workflow",
  "",
  "Use compound_create for a complete editable object. Each part must use either mesh for an existing cached/file-backed mesh or shape for a generated parametric mesh. For repeated generated parts, give them the same explicit mesh_path and set reuse_existing true. Treat explicit mesh paths as immutable cache keys and use a new path when parameters change. Set prefab_path when the result should become a reusable asset; serialized render components retain file paths so prefab loading can restore generated meshes and materials.",
  "",
  "## Coordinate and dimension rules",
  "",
  "- units are meters",
  "- y is up",
  "- size is full width, height, depth",
  "- positions and rotations on compound parts are local to the compound root",
  "- extruded profiles use counter clockwise x,y points and extrude along z",
  "- revolved profiles use radius,y points and revolve around y",
  "",
  "## Shape selection",
  "",
  "- beveled_box: desks, cabinets, monitors, keyboards, shelves, doors, and trim",
  "- rounded_box: mice, cushions, upholstery, appliances, and soft plastic shells",
  "- wedge: ramps, angled supports, sloped shades, and simple roofs",
  "- wall_opening: walls with real door or window voids and no hidden backing cube",
  "- wall_openings: one wall with up to 16 real rectangular door and window voids",
  "- extruded_profile: convex or concave custom wall outlines, frames, handles, brackets, and silhouettes",
  "- revolved_profile: lamp bases, knobs, bottles, table legs, and shades",
  "- torus: rings, handles, bezels, wheels, and cable loops",
  "- capsule: handles, cushions, soft supports, and rounded controls",
  "- rounded_cylinder: knobs, feet, cups, buttons, and compact appliances",
  "- pipe: cables, rails, plumbing, and curved trim along path_points",
  "- curved_profile: swept custom cross sections for rails, molding, and frames",
  "- loft: changing cross sections along path_points for roofs, ducts, hulls, and housings",
  "- arch: doorways, alcoves, curved windows, and architectural trim",
  "- inset_panel: cabinet doors, drawers, wall panels, and decorative fronts",
  "- tapered_extrusion: lampshades, supports, handles, and tapered housings",
  "- grid: subdivided ground, terrain patches, and surfaces that need later deformation",
  "- grass_blade: reusable procedural foliage blades for instancing",
  "- flower: reusable stem and petal geometry for environment detail",
  "- modifiers: use taper, bend, mirror, shell, linear or radial arrays, then planar, box, or cylindrical uv projection",
  "- sweep controls: use one scale and twist value per path point for variable pipes and curved profiles",
  "- uv seams: set uv_split_seams with box projection for clean face islands and hard edges",
  "",
  "## Budgets",
  "",
  "- rounded_box segments: 2 for background props, 4 for normal furniture, 6 to 8 for close views, maximum 16",
  "- revolved_profile segments: 12 for small details, 24 for normal props, 32 to 48 for close views, maximum 64",
  "- profiles: 3 to 32 simple counter clockwise points, concave extrusion is supported",
  "- generated mesh limit: 100000 vertices and 300000 indices",
  "- compound limit: 64 parts; reuse meshes for repeated keys, shelves, legs, and slats",
  "",
  "## Dimensional patterns",
  "",
  "### Desk",
  "Use a 1.4 x 0.06 x 0.7 beveled_box top at y 0.75, four reused 0.06 x 0.72 x 0.06 beveled_box legs, and optional 0.45 x 0.04 x 0.18 drawer parts.",
  "",
  "### Monitor",
  "Use a 0.62 x 0.36 x 0.045 beveled_box body at eye height, a slightly forward screen part with a dark material, a 0.05 x 0.24 x 0.05 neck, and a 0.28 x 0.025 x 0.2 rounded_box base.",
  "",
  "### Keyboard",
  "Use a 0.45 x 0.025 x 0.15 beveled_box body and one reused 0.018 x 0.008 x 0.018 rounded_box mesh for key entities in staggered rows. Use fewer grouped key parts for distant scenes.",
  "",
  "### Mouse",
  "Use a 0.07 x 0.035 x 0.115 rounded_box with radius 0.017 and 6 segments. Add a thin wheel from a revolved profile only for close views.",
  "",
  "### Chair",
  "Use a 0.46 x 0.05 x 0.46 rounded_box seat at y 0.48, a 0.46 x 0.55 x 0.05 rounded_box back, and reused beveled_box or revolved_profile legs.",
  "",
  "### Shelf",
  "Use two vertical 0.04 x 1.8 x 0.35 beveled_box sides and several instances of one 0.72 x 0.035 x 0.35 shelf mesh. Keep shelf spacing between 0.28 and 0.38.",
  "",
  "### Door",
  "Use a 0.9 x 2.05 x 0.045 beveled_box slab, three extruded_profile frame pieces or beveled boxes, and a small revolved_profile handle at y 1.0.",
  "",
  "### Window",
  "Use four thin beveled_box frame pieces around an opening, one thin center pane with a glass material, and optional repeated mullions. Do not place a wall cube behind the opening.",
  "",
  "### Lamp",
  "Use a revolved_profile base and stem, plus a revolved_profile or wedge shade. Add the actual light separately with entity_create_light and calibrated intensity.",
  "",
  "## Material guidance",
  "",
  "Call material_palette_create before compound_create. Use distinct semantic materials for walls, wood, plastic, screens, metals, fabric, glass, concrete, asphalt, masonry, rubber, road paint, and emissive details. Palette input colors are converted from sRGB to linear automatically and receive coordinated PBR finish rules.",
  "",
  "## Placement and review",
  "",
  "Call entity_snap after creating furniture and props. Use floor for desks, beds, chairs, and shelves; wall for monitors, frames, and cabinets; ceiling for hanging lights; surface for arbitrary orientations.",
  "",
  "Call scene_visual_review after each major construction pass. Correct visible floating parts, intersections, wrong scale, missing materials, weak lighting, and repetitive silhouettes. Repeat until the image and structured material audit agree with the requested scene.",
].join("\n");

export const edit_rules = [
  "# Spartan MCP Edit Rules",
  "",
  "Read tools may run while the world is ready.",
  "Mutating tools require edit mode by default.",
  "World loading blocks most tools until loading completes.",
  "Destructive tools should return receipts with deleted ids, counts, and partial progress when possible.",
  "Read-only mode registers only inspection and knowledge tools.",
  "Use world_save only after user intent is clear.",
].join("\n");

export function component_schema_markdown() {
  const lines = ["# Spartan Component Property Schemas", "", "component_get also returns editable_members and members for raw registered component fields. component_set accepts either friendly properties or those raw member/property names.", ""];
  for (const [type, entry] of Object.entries(component_property_catalog)) {
    lines.push(`## ${type}`);
    lines.push(entry.notes);
    lines.push("");
    lines.push(entry.properties.map((property) => `- ${property}`).join("\n"));
    lines.push("");
  }
  return lines.join("\n").trimEnd();
}

export function capability_catalog(tool_registry, resource_registry) {
  const tools = [...tool_registry.values()].map((tool) => ({
    name: tool.name,
    title: tool.title,
    description: tool.description,
    read_only: Boolean(tool.annotations?.readOnlyHint),
    destructive: Boolean(tool.annotations?.destructiveHint),
  }));

  const resources = [...resource_registry.values()].map((resource) => ({
    name: resource.name,
    uri: resource.uri,
    title: resource.title,
    description: resource.description,
    read_only: true,
  }));

  return { tools, resources };
}

export function search_capability_catalog(tool_registry, resource_registry, query, limit = 8) {
  const terms = String(query ?? "")
    .toLowerCase()
    .split(/[^a-z0-9_]+/g)
    .filter((term) => term.length > 1);

  const catalog = capability_catalog(tool_registry, resource_registry);
  const entries = [
    ...catalog.tools.map((entry) => ({ kind: "tool", ...entry })),
    ...catalog.resources.map((entry) => ({ kind: "resource", ...entry })),
  ];

  const scored = [];
  for (const entry of entries) {
    const haystack = `${entry.name} ${entry.title ?? ""} ${entry.description ?? ""}`.toLowerCase();
    let score = 0;
    for (const term of terms) {
      if (entry.name.toLowerCase().includes(term)) {
        score += 4;
      }
      if (haystack.includes(term)) {
        score += 1;
      }
    }
    if (score > 0 || terms.length === 0) {
      scored.push({ score, ...entry });
    }
  }

  scored.sort((a, b) => b.score - a.score || a.name.localeCompare(b.name));
  return scored.slice(0, Math.max(1, Math.min(25, limit)));
}
