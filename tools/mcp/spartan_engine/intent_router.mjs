function normalized(prompt) {
  return String(prompt ?? "").toLowerCase().replace(/\s+/g, " ").trim();
}

export function should_use_selected_entity(prompt) {
  return /\b(this|selected|current)\s+entity\b/.test(normalized(prompt));
}

const target_name_stopwords = new Set([
  "this", "selected", "current", "the", "an", "a", "entity", "entities", "children", "child", "contents",
  "parent", "under", "called", "named", "existing", "new", "area", "scene", "world", "level", "blockout",
  "there", "is", "are", "its", "it's", "their", "our", "your", "my", "some", "any", "all",
]);

function clean_target_name(raw) {
  const value = String(raw ?? "").trim().replace(/^["'`]+|["'`]+$/g, "").replace(/\s+/g, " ");
  if (!value)
  {
    return "";
  }

  const tokens = value.toLowerCase().split(/[\s-]+/).filter(Boolean);
  if (tokens.length === 0 || tokens.every((token) => target_name_stopwords.has(token)))
  {
    return "";
  }

  if (tokens.length === 1)
  {
    return tokens[0];
  }

  // prefer snake_case style names, drop leading filler words like parent under an
  const meaningful = tokens.filter((token) => !target_name_stopwords.has(token));
  if (meaningful.length === 1)
  {
    return meaningful[0];
  }
  if (meaningful.length > 1 && meaningful.every((token) => /^[a-z][a-z0-9]*$/.test(token)))
  {
    return meaningful.join("_");
  }

  return "";
}

export function target_name_from_prompt(prompt) {
  const value = normalized(prompt);

  const quoted_match = value.match(/\b(?:entity|parent|root)?\s*(?:called|named)\s+["']([a-z0-9 _-]+)["']/i)
    || value.match(/["']([a-z][a-z0-9]*(?:_[a-z0-9]+)+)["']/);
  if (quoted_match?.[1])
  {
    const cleaned = clean_target_name(quoted_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const called_match = value.match(/\b(?:entity|parent|root)?\s*(?:called|named)\s+([a-z][a-z0-9_-]*)\b/);
  if (called_match?.[1])
  {
    const cleaned = clean_target_name(called_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const under_match = value.match(/\b(?:parent(?:ed)?\s+)?under\s+(?:an?\s+)?(?:entity\s+)?(?:called|named)\s+([a-z][a-z0-9_-]*)\b/);
  if (under_match?.[1])
  {
    const cleaned = clean_target_name(under_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const existing_match = value.match(/\b(?:existing|the)\s+entity\s+["']?([a-z][a-z0-9_-]*)["']?\b/);
  if (existing_match?.[1])
  {
    const cleaned = clean_target_name(existing_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const explicit_entity_match = value.match(/\b([a-z][a-z0-9]*(?:_[a-z0-9]+)+)\b/);
  if (explicit_entity_match?.[1])
  {
    return explicit_entity_match[1].trim();
  }

  const named_match = value.match(/\bnamed\s+["']?([a-z0-9 _-]+?)["']?(?:\s|,|\.|$)/);
  if (named_match?.[1])
  {
    const cleaned = clean_target_name(named_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const children_match = value.match(/children\s+of\s+(?:the\s+)?([a-z0-9 _-]+?)\s+entity\b/);
  if (children_match?.[1])
  {
    const cleaned = clean_target_name(children_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const delete_match = value.match(/\b(?:delete|remove|destroy)\s+(?:the\s+)?["']?([a-z0-9 _-]+?)["']?(?:\s+entity)?(?:\s|,|\.|$)/);
  if (delete_match?.[1])
  {
    const cleaned = clean_target_name(delete_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  const entity_match = value.match(/\b([a-z0-9 _-]+?)\s+entity\b/);
  if (entity_match?.[1])
  {
    const cleaned = clean_target_name(entity_match[1]);
    if (cleaned)
    {
      return cleaned;
    }
  }

  return "";
}

function is_delete_children_request(value) {
  const wants_delete = /\b(delete|remove|clear|wipe)\b/.test(value);
  const mentions_children = /\b(children|child|entities|contents)\b/.test(value);
  return wants_delete && mentions_children;
}

function is_delete_entity_request(value) {
  const wants_delete = /\b(delete|remove|destroy)\b/.test(value);
  const mentions_children = /\b(children|child|contents)\b/.test(value);
  const mentions_target = /\b(selected|current|this|entity)\b/.test(value) || target_name_from_prompt(value) !== "";
  return wants_delete && mentions_target && !mentions_children;
}

function is_rebuild_scene_request(value) {
  const destructive = /\b(delete|remove|replace|rebuild|redo|remake|recreate)\b/.test(value);
  const constructive = /\b(create|make|build|generate|rebuild|remake|recreate|bigger|larger|architecture|hallways?|corridors?|rooms?|open areas?|columns?)\b/.test(value);
  const scene_target = /\b(room|level|area|scene|geometry|construct|environment|blockout)\b/.test(value) || target_name_from_prompt(value) !== "";
  return destructive && constructive && scene_target && !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value);
}

function is_scene_construction_request(value) {
  const constructive = /\b(create|make|build|uild|generate|construct|blockout|layout|lay out|design|place)\b/.test(value);
  const scene_target = /\b(room|rooms|level|levels|area|scene|geometry|environment|blockout|hallway|hallways|corridor|corridors|maze|map|interior|space|backrooms|liminal)\b/.test(value);
  const code_context = /\b(source|code|file|files|cpp|c\+\+|javascript|compile|compilation|build error|build failed|build system|git|diff|commit|function|class|implementation)\b/.test(value);
  return constructive && scene_target && !code_context;
}

function primitive_from_prompt(value) {
  for (const primitive of ["cone", "cylinder", "sphere", "cube", "quad", "plane"]) {
    if (new RegExp(`\\b${primitive}\\b`).test(value)) {
      return primitive === "plane" ? "quad" : primitive;
    }
  }
  return "";
}

function default_body_type_for_primitive(mesh) {
  if (mesh === "cube") {
    return "box";
  }
  if (mesh === "quad") {
    return "plane";
  }
  if (mesh === "sphere") {
    return "sphere";
  }
  if (mesh === "cylinder") {
    return "capsule";
  }
  if (mesh === "cone") {
    return "mesh_convex";
  }
  return undefined;
}

function is_create_primitive_request(value) {
  const wants_create = /\b(create|make|spawn|add)\b/.test(value);
  return wants_create && primitive_from_prompt(value) !== "" && !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value);
}

function is_live_scene_edit_request(value) {
  const edit_verb = /\b(create|make|spawn|add|place|put|move|delete|remove|destroy|rotate|scale|select|build|clear|wipe)\b/.test(value);
  const scene_object = /\b(entity|entities|world|scene|primitive|mesh|cube|box|quad|plane|sphere|ball|cylinder|cone|camera|light|physics|rigidbody|collider|track|ramp|room|rooms|level|levels|area|environment|hallway|hallways|corridor|corridors|backrooms|liminal)\b/.test(value);
  return edit_verb && scene_object && !/\b(source|code|file|cpp|c\+\+|javascript|compile|build error|git|diff)\b/.test(value);
}

function number_from_text(value) {
  const words = new Map([
    ["zero", 0],
    ["one", 1],
    ["two", 2],
    ["three", 3],
    ["four", 4],
    ["five", 5],
    ["six", 6],
    ["seven", 7],
    ["eight", 8],
    ["nine", 9],
    ["ten", 10],
  ]);
  const numeric = Number.parseFloat(value);
  if (Number.isFinite(numeric)) {
    return numeric;
  }
  return words.get(value) ?? null;
}

function distance_match(value, pattern) {
  const match = value.match(pattern);
  if (!match?.[1]) {
    return undefined;
  }

  const distance = number_from_text(match[1]);
  return distance === null ? undefined : distance;
}

function primitive_position_constraints(value) {
  return {
    height_above_ground: distance_match(value, /\b(\d+(?:\.\d+)?|zero|one|two|three|four|five|six|seven|eight|nine|ten)\s*(?:units?|meters?|metres?|m)?\s+(?:above|over)\s+(?:the\s+)?ground\b/),
    camera_forward_distance: distance_match(value, /\b(\d+(?:\.\d+)?|zero|one|two|three|four|five|six|seven|eight|nine|ten)\s*(?:units?|meters?|metres?|m)?\s+(?:in\s+front\s+of|ahead\s+of|from)\s+(?:the\s+)?camera\b/),
    use_ground_raycast: /\b(on|onto|snap(?:ped)? to|rest(?:ing)? on)\s+(?:the\s+)?(?:ground|terrain|surface)\b/.test(value),
  };
}

function primitive_name_from_prompt(value, mesh, with_physics) {
  const named_match = value.match(/\bnamed\s+["']?([a-z0-9 _-]+?)["']?(?:\s|,|\.|$)/);
  if (named_match?.[1]) {
    return named_match[1].trim();
  }

  const label = mesh === "quad" ? "Quad" : mesh.charAt(0).toUpperCase() + mesh.slice(1);
  return with_physics ? `Physics ${label}` : label;
}

function physics_static_from_prompt(value) {
  if (/\b(static|fixed|immovable|non[- ]?dynamic|not dynamic|anchored)\b/.test(value)) {
    return true;
  }
  if (/\b(dynamic|movable|fall(?:ing)?|non[- ]?static|not static)\b/.test(value)) {
    return false;
  }
  return undefined;
}

function is_simple_read_request(value) {
  return (
    /\b(what is selected|what's selected|selected entity|current selection)\b/.test(value) ||
    /\b(summarize|summary|status|inspect)\b/.test(value) && /\b(world|scene|engine)\b/.test(value) ||
    /\b(list|show)\b/.test(value) && /\b(primitive types|component types)\b/.test(value)
  );
}

function engine_mode_from_prompt(value) {
  if (/\b(pause|paused)\b/.test(value)) {
    return "pause";
  }
  if (/\b(resume|unpause)\b/.test(value)) {
    return "resume";
  }
  if (/\b(play|run|start simulation|start game)\b/.test(value)) {
    return "play";
  }
  if (/\b(edit mode|stop playing|stop simulation|stop game)\b/.test(value)) {
    return "edit";
  }
  return "";
}

function is_engine_mode_request(value) {
  return /\b(play|run|pause|resume|unpause|edit mode|stop playing|stop simulation|start simulation)\b/.test(value) &&
    !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value) &&
    !/\b(sequencer|sequence|timeline|cinematic|camera cut)\b/.test(value);
}

function is_console_request(value) {
  return /\b(console|log|logs|errors|warnings|crash|stack)\b/.test(value) &&
    /\b(read|show|list|what|latest|recent|last|check|inspect)\b/.test(value);
}

function is_mcp_status_request(value) {
  return /\b(mcp|assistant|bridge|code index)\b/.test(value) &&
    /\b(status|health|ready|connected|broken|diagnose)\b/.test(value) &&
    !/\b(car|camera|cut|cuts|spline|sequencer|sequence|speed|entity|scene|world)\b/.test(value);
}

function is_calibrate_lights_request(value) {
  const mentions_lights = /\blights?\b/.test(value);
  const wants_calibrate = /\b(calibrat\w*|fix|tune|adjust|boost|strengthen|make (?:them |the lights )?visible)\b/.test(value);
  const all_or_scene = /\b(all|every|scene|world|existing)\b/.test(value) || /\bbased on (?:their|its) nature\b/.test(value);
  return mentions_lights && wants_calibrate && (all_or_scene || /\bnature\b/.test(value));
}

function is_city_develop_request(value) {
  const wants_city = /\b(road|roads|spline|highway|street|connect|link|network|district|landmark|city)\b/.test(value);
  const constructive = /\b(create|make|build|connect|link|lay|add|generate|scan|decorate|develop)\b/.test(value);
  const has_targets = /\b(gas_station|dockyard|airway|airport|playground|landmark|between|from|to|map|world|areas?)\b/.test(value) || /\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+\b/.test(value);
  return wants_city && constructive && has_targets && !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value);
}

function landmarks_from_prompt(prompt) {
  const value = normalized(prompt);
  const found = [];
  const push = (name) =>
  {
    if (name && !found.includes(name))
    {
      found.push(name);
    }
  };

  if (/\bgas[_\s-]?station\b/.test(value))
  {
    push("gas_station");
  }
  if (/\bdock[_\s-]?yard\b/.test(value))
  {
    push("dockyard");
  }
  if (/\b(airway|airport)\b/.test(value))
  {
    push("airway");
  }
  if (/\bplayground\b/.test(value))
  {
    push("playground");
  }

  const snake = value.match(/\b([a-z][a-z0-9]*(?:_[a-z0-9]+)+)\b/g) ?? [];
  for (const name of snake)
  {
    if (["spline_road", "road_width", "control_point"].includes(name))
    {
      continue;
    }
    push(name);
  }

  return found;
}

function is_source_code_request(value) {
  const asks_about_code = /\b(source|code|file|files|cpp|c\+\+|javascript|script|compile|compilation|build error|build failed|build system|git|diff|commit|bug|crash|stack|log|function|class|where|implementation)\b/.test(value);
  const live_scene_action = /\b(entity|world|scene|selection|selected|create|delete|spawn|ramp|cone|room|rooms|level|levels|area|environment|hallway|hallways|corridor|corridors|backrooms|liminal)\b/.test(value) &&
    /\b(create|delete|spawn|move|rotate|scale|select|clear|build|make)\b/.test(value);
  return asks_about_code && !live_scene_action;
}

// fast paths exist for terse commands, long prompts describe real work and must reach the full agent
const fast_path_max_length = 160;

export function route_intent(prompt) {
  const value = normalized(prompt);
  if (!value) {
    return { kind: "none", confidence: 0 };
  }

  const is_terse = value.length <= fast_path_max_length;

  if (is_terse && is_simple_read_request(value)) {
    return { kind: "simple_read", confidence: 0.92 };
  }

  if (is_terse && is_mcp_status_request(value)) {
    return { kind: "mcp_status", confidence: 0.94 };
  }

  if (is_terse && is_console_request(value)) {
    const minimum_type = /\berrors?\b/.test(value) ? "error" : /\bwarnings?\b/.test(value) ? "warning" : undefined;
    return { kind: "console_read", confidence: 0.93, minimum_type };
  }

  if (is_terse && is_engine_mode_request(value)) {
    return { kind: "engine_mode", confidence: 0.9, mode: engine_mode_from_prompt(value) };
  }

  if (is_calibrate_lights_request(value))
  {
    return {
      kind: "calibrate_lights",
      confidence: 0.94,
      live_scene_action: true,
      allow_cursor_fallback: false,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_city_develop_request(value))
  {
    return {
      kind: "city_develop",
      confidence: 0.92,
      live_scene_action: true,
      allow_cursor_fallback: true,
      landmarks: landmarks_from_prompt(prompt),
    };
  }

  if (is_rebuild_scene_request(value))
  {
    return {
      kind: "scene_rebuild",
      confidence: 0.9,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_scene_construction_request(value))
  {
    return {
      kind: "scene_rebuild",
      confidence: 0.88,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_terse && is_create_primitive_request(value)) {
    const mesh = primitive_from_prompt(value);
    const with_physics = /\b(physics|physical|rigidbody|rigid body|collision|collider|dynamic)\b/.test(value);
    const position_constraints = primitive_position_constraints(value);
    const physics_static = physics_static_from_prompt(value);
    return {
      kind: "create_primitive",
      confidence: 0.94,
      live_scene_action: true,
      allow_cursor_fallback: false,
      mesh,
      name: primitive_name_from_prompt(value, mesh, with_physics),
      with_physics,
      body_type: with_physics ? default_body_type_for_primitive(mesh) : undefined,
      physics_static: with_physics ? physics_static ?? false : undefined,
      position: position_constraints.height_above_ground !== undefined ? [0, position_constraints.height_above_ground, 0] : undefined,
      ...position_constraints,
    };
  }

  if (is_terse && is_delete_children_request(value)) {
    return {
      kind: "delete_children",
      confidence: 0.95,
      live_scene_action: true,
      allow_cursor_fallback: false,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_terse && is_delete_entity_request(value)) {
    return {
      kind: "delete_entity",
      confidence: 0.95,
      live_scene_action: true,
      allow_cursor_fallback: false,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_terse && is_source_code_request(value)) {
    return { kind: "source_code", confidence: 0.9 };
  }

  if (is_live_scene_edit_request(value)) {
    return {
      kind: "live_scene_edit",
      confidence: 0.82,
      live_scene_action: true,
      allow_cursor_fallback: true,
    };
  }

  return { kind: "cursor", confidence: 0.4, allow_cursor_fallback: true };
}
