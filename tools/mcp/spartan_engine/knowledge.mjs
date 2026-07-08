export const component_property_catalog = {
  render: {
    properties: ["mesh", "material", "default_material", "visible", "casts_shadows", "exclude_from_ray_tracing", "max_render_distance", "max_shadow_distance"],
    notes: "controls mesh, material, visibility, shadowing, and render distance for a renderable entity",
  },
  physics: {
    properties: ["body_type", "static", "kinematic", "enabled", "mass", "friction", "friction_rolling", "restitution", "center_of_mass", "linear_velocity", "angular_velocity"],
    notes: "controls rigid body type, mass properties, velocities, and physics state",
  },
  light: {
    properties: ["light_type", "color", "temperature", "intensity", "range", "angle_degrees", "area_width", "area_height", "shadows", "volumetric", "draw_distance", "shadow_distance", "volumetric_distance"],
    notes: "controls light type, photometric values, color, range, angle, area size, and shadow or volumetric flags",
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
  "Use entity_find_by_component to locate all entities with a given component type without listing the whole scene.",
  "Read component_get property_metadata and member_metadata for ranges, units, enum values, side effects, recommended defaults, and read-only reasons before unfamiliar component writes.",
  "Use component_set_batch when changing multiple properties on the same component.",
  "Use entity_set_transform_batch when repositioning many entities; one call replaces a run of entity_set_transform calls.",
  "Lua resolves entities with World.GetEntityByName(name) for exact names or World.GetEntityById(id) with the id as a string.",
  "Use component_action for deterministic component methods such as terrain generation, spline mesh/instance operations, particle presets/bursts, physics forces, audio playback, light fitting, and camera focus.",
  "Use resource_list and material_get to inspect cached assets and material state; use material_set_property and material_set_texture for material edits.",
  "Use resource_load, resource_reload, resource_save, resource_remove, and material_create for asset cache lifecycle work.",
  "Use undo_redo for editor command-stack undo and redo.",
  "Use camera_set_view and viewport_frame for editor camera positioning; use renderer_debug_set and physics_state for debug inspection.",
  "Use profiler_snapshot to read frame timing, fps, stutter flags, rhi counters, and per-pass CPU/GPU time blocks; sort by duration and inspect the top blocks to find optimization targets.",
  "Use selection_update, entity_clone, entity_move_index, prefab_types, prefab_save, and prefab_load before Lua for common editor hierarchy and prefab workflows.",
  "Use screenshot_take when visual verification is useful; it waits briefly for the async save and can return the PNG as image content.",
  "Before destructive rebuilds that should preserve appearance, call entity_render_materials and reuse the returned material names on new render geometry.",
  "Use entity_create_light for point, spot, directional, and area lights; calibrate intensity, range, and area size to the room or blockout scale.",
  "Use debug_log_read or spartan://agent/debug-log to inspect assistant prompts, engine command arguments, durations, and outputs.",
  "Use async_task_start and async_task_get for long-running MCP tools that should be reported without blocking the main client request.",
  "Live scene edits should route through deterministic tools when one matches, and fall back to the Cursor agent with the engine MCP tools otherwise.",
  "Treat scene construction prompts such as build a level, make rooms, backrooms, and liminal space as live scene edits, not source-code search requests.",
  "Missing deterministic capabilities should be recorded in agent memory under Problem Reports immediately.",
  "For procedural scene edits, execute_lua is the broad capability layer, but it is edit-mode guarded by default.",
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
