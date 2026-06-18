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
};

export const engine_overview = [
  "# Spartan Engine MCP",
  "",
  "Spartan exposes a live editor and runtime control surface through a local C++ TCP bridge on 127.0.0.1:47777.",
  "The Node MCP adapter exposes stable tools over stdio for external agents and forwards commands to the engine bridge.",
  "The in-editor assistant talks to assistant.mjs on 127.0.0.1:47778 and uses deterministic fast paths before escalating to Cursor.",
  "",
  "Prefer context_snapshot, camera_snapshot, world_raycast, entity_resolve, component_get, and native batch tools before issuing many small commands.",
  "Use debug_log_read or spartan://agent/debug-log to inspect assistant prompts, engine command arguments, durations, and outputs.",
  "Live scene edits should route through deterministic tools or recipes and fail fast when no deterministic operation exists.",
  "Missing deterministic capabilities should be recorded in agent memory under Problem Reports immediately.",
  "For procedural scene edits, execute_lua is the broad capability layer, but it is edit-mode guarded by default.",
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
  const lines = ["# Spartan Component Property Schemas", ""];
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
