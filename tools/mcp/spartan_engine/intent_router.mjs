function normalized(prompt) {
  return String(prompt ?? "").toLowerCase().replace(/\s+/g, " ").trim();
}

export function should_use_selected_entity(prompt) {
  return /\b(this|selected|current)\s+entity\b/.test(normalized(prompt));
}

export function target_name_from_prompt(prompt) {
  const value = normalized(prompt);
  const named_match = value.match(/\bnamed\s+["']?([a-z0-9 _-]+?)["']?(?:\s|,|\.|$)/);
  if (named_match?.[1]) {
    return named_match[1].trim();
  }

  if (value.includes("playground")) {
    return "playground";
  }

  const children_match = value.match(/children\s+of\s+(?:the\s+)?([a-z0-9 _-]+?)\s+entity\b/);
  if (children_match?.[1] && !["this", "selected", "current"].includes(children_match[1].trim())) {
    return children_match[1].trim();
  }

  const delete_match = value.match(/\b(?:delete|remove|destroy)\s+(?:the\s+)?["']?([a-z0-9 _-]+?)["']?(?:\s+entity)?(?:\s|,|\.|$)/);
  if (delete_match?.[1]) {
    const target = delete_match[1].trim();
    if (!["this", "selected", "current", "the", "entity", "children", "child", "contents"].includes(target)) {
      return target;
    }
  }

  const entity_match = value.match(/\b([a-z0-9 _-]+?)\s+entity\b/);
  if (entity_match?.[1] && !["this", "selected", "current", "the"].includes(entity_match[1].trim())) {
    return entity_match[1].trim();
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
  const mentions_target = /\b(selected|current|this|entity|playground)\b/.test(value) || target_name_from_prompt(value) !== "";
  return wants_delete && mentions_target && !mentions_children;
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
    !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value);
}

function is_console_request(value) {
  return /\b(console|log|logs|errors|warnings|crash|stack)\b/.test(value) &&
    /\b(read|show|list|what|latest|recent|last|check|inspect)\b/.test(value);
}

function is_mcp_status_request(value) {
  return /\b(mcp|assistant|agent|bridge|index|tools?)\b/.test(value) &&
    /\b(status|health|ready|working|connected|slow|broken|diagnose|check)\b/.test(value);
}

function is_car_playground_request(value) {
  const wants_build = /\b(build|create|make|generate|spawn|blockout)\b/.test(value);
  return wants_build && /\b(playground|course|test track|track)\b/.test(value) && /\b(car|vehicle|driving)\b/.test(value);
}

function is_source_code_request(value) {
  const asks_about_code = /\b(source|code|file|files|cpp|c\+\+|javascript|script|compile|build|git|diff|commit|bug|crash|stack|log|function|class|where|implementation)\b/.test(value);
  const live_scene_action = /\b(entity|world|scene|playground|selection|selected|create|delete|spawn|ramp|cone|car)\b/.test(value) &&
    /\b(create|delete|spawn|move|rotate|scale|select|clear|build|make)\b/.test(value);
  return asks_about_code && !live_scene_action;
}

export function route_intent(prompt) {
  const value = normalized(prompt);
  if (!value) {
    return { kind: "none", confidence: 0 };
  }

  if (is_simple_read_request(value)) {
    return { kind: "simple_read", confidence: 0.92 };
  }

  if (is_mcp_status_request(value)) {
    return { kind: "mcp_status", confidence: 0.94 };
  }

  if (is_console_request(value)) {
    const minimum_type = /\berrors?\b/.test(value) ? "error" : /\bwarnings?\b/.test(value) ? "warning" : undefined;
    return { kind: "console_read", confidence: 0.93, minimum_type };
  }

  if (is_engine_mode_request(value)) {
    return { kind: "engine_mode", confidence: 0.9, mode: engine_mode_from_prompt(value) };
  }

  if (is_car_playground_request(value)) {
    return {
      kind: "car_playground",
      confidence: 0.88,
      target_name: target_name_from_prompt(prompt) || "playground",
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_delete_children_request(value)) {
    return {
      kind: "delete_children",
      confidence: 0.95,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_delete_entity_request(value)) {
    return {
      kind: "delete_entity",
      confidence: 0.95,
      target_name: target_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_source_code_request(value)) {
    return { kind: "source_code", confidence: 0.9 };
  }

  return { kind: "cursor", confidence: 0.4 };
}
