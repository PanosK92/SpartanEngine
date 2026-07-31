function normalized(prompt) {
  return String(prompt ?? "").toLowerCase().replace(/\s+/g, " ").trim();
}

export function should_use_selected_entity(prompt) {
  return /\b(this|selected|current)\s+entity\b/.test(normalized(prompt));
}

const target_name_stopwords = new Set([
  "this", "selected", "current", "the", "an", "a", "entity", "entities", "children", "child", "contents",
  "parent", "under", "called", "named", "existing", "new", "area", "scene", "world", "level", "blockout",
  "there", "is", "are", "it", "its", "it's", "their", "our", "your", "my", "some", "any", "all",
  "seam_error", "asset_id", "root_id", "entity_id", "parent_id",
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

  const under_entity_match = value.match(
    /\bunder\s+(?:the\s+)?(?:entity|parent)\s+(?:(?:called|named)\s+)?["']?([a-z][a-z0-9_-]*)["']?\b/,
  );
  if (under_entity_match?.[1])
  {
    const cleaned = clean_target_name(
      under_entity_match[1],
    );
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
    const cleaned = clean_target_name(
      explicit_entity_match[1],
    );
    if (cleaned)
    {
      return cleaned;
    }
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

// the place a request is about, with its qualifier when it has one, as in gas station
//
// a place word is often a modifier rather than the subject. an office chair is a chair and a garage door is
// a door, so a place word only names the build when nothing follows that it could be describing
function place_phrase_from_prompt(value) {
  const place_heads =
    "shop|store|station|factory|warehouse|office|house|building|garage|workshop|lounge|bar|airport|hotel|school|hospital|museum|arena|garden|park|plaza";
  // the qualifier cannot be an article, an office is not a kind of office
  const pattern = new RegExp(
    `\\b(?:((?!an?\\b|the\\b|my\\b|some\\b)[a-z0-9-]+)\\s+)?(${place_heads})\\b`,
    "g",
  );
  const connective_follows =
    /^(?:with|that|which|containing|under|inside|for|and|in|on|at|to|near|around|from|of|by|so|then|please)\b/;

  for (const found of value.matchAll(pattern))
  {
    const rest = value
      .slice(found.index + found[0].length)
      .trim();
    if (rest.length > 0 && !connective_follows.test(rest))
    {
      continue;
    }

    return found[1]
      ? `${found[1]} ${found[2]}`
      : found[2];
  }
  return "";
}

export function scene_root_name_from_prompt(prompt) {
  const value = normalized(prompt);
  const explicit_target = target_name_from_prompt(prompt);
  const continues_existing =
    /\b(continue|finish|complete|audit|review|correct|polish|improve|refine|enhance|upgrade|detail|beautify|existing)\b/.test(
      value,
    );
  if (continues_existing && explicit_target)
  {
    return explicit_target;
  }
  const has_explicit_root =
    /\b(?:entity|parent|root)\s+(?:called|named)\b/.test(value) ||
    /\bparent(?:ed)?\s+under\s+(?:an?\s+)?(?:entity\s+)?(?:called|named)\b/.test(value) ||
    /\bunder\s+(?:the\s+)?(?:entity|parent)\b/.test(value);
  if (has_explicit_root)
  {
    const explicit = target_name_from_prompt(prompt);
    if (explicit)
    {
      return explicit;
    }
  }

  const place_phrase = place_phrase_from_prompt(value);
  if (place_phrase)
  {
    const place_name = clean_target_name(place_phrase);
    if (place_name)
    {
      return place_name;
    }
  }

  const match = value.match(
    /\b(?:create|make|build|generate|construct|design|continue|finish|complete|polish|improve|refine|enhance|upgrade|detail|beautify)\s+(?:me\s+)?(?:an?\s+|the\s+)?([a-z0-9][a-z0-9 _-]{0,60}?)(?=\s+(?:with|that|which|containing|under|inside|for)\b|[,.;]|$)/,
  );
  if (!match?.[1])
  {
    return "generated_environment";
  }

  const cleaned = clean_target_name(
    strip_deliverable_words(match[1]),
  );
  return cleaned || "generated_environment";
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
  const actionable_value = value.replace(
    /\b(?:do not|don't|never)\s+(?:delete|remove|replace|rebuild|redo|remake|recreate)\b/g,
    "",
  );
  const destructive = /\b(delete|remove|replace|rebuild|redo|remake|recreate)\b/.test(actionable_value);
  const constructive = /\b(create|make|build|generate|rebuild|remake|recreate|bigger|larger|architecture|hallways?|corridors?|rooms?|open areas?|columns?)\b/.test(actionable_value);
  const scene_target = /\b(room|level|area|scene|geometry|construct|environment|blockout)\b/.test(value) || target_name_from_prompt(value) !== "";
  return destructive && constructive && scene_target && !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(value);
}

function is_scene_construction_request(value) {
  const constructive = /\b(create|make|build|uild|generate|construct|blockout|layout|lay out|design|place|continue|finish|complete|audit|review|correct|polish|improve|refine|enhance|upgrade|detail|beautify|dress)\b/.test(value);
  const scene_target = /\b(rooms?|levels?|areas?|scenes?|geometry|environments?|blockouts?|hallways?|corridors?|mazes?|maps?|interiors?|spaces?|backrooms|liminal|playgrounds?|parks?|factories|factory|warehouses?|stations?|gas stations?|streets?|plazas?|offices?|houses?|buildings?|landscapes?|arenas?|yards?|gardens?|cities|city|districts?|downtown|neighborhoods?|garages?|workshops?|lounges?|bars?|airports?|shops?|stores?|cafes?|coffee shops?|restaurants?|hotels?|schools?|hospitals?|museums?|facilities|venues?|places?)\b/.test(value);
  const generic_verb =
    /\b(build|construct|design|generate)\b/.test(value) ||
    /\b(create|make)\s+(?:me\s+)?(?:an?\s+|the\s+)[a-z]/.test(value);
  const generic_build =
    generic_verb &&
    !/\b(cube|sphere|quad|plane|cylinder|cone|material|texture|shader|script|file|light|camera|entity|component|mesh|primitive)\b/.test(value);
  const code_context = /\b(source|code|file|files|cpp|c\+\+|javascript|compile|compilation|build error|build failed|build system|git|diff|commit|function|class|implementation)\b/.test(value);
  return (
    constructive &&
    (scene_target || generic_build) &&
    !code_context
  );
}

// nouns that name a place or a whole scene, a change aimed at one of those is scene work however it is
// phrased, so they are what keeps make the garage bigger away from the asset revision path
const scene_subject_pattern =
  /\b(scene|level|map|world|environment|city|district|downtown|street|road|neighbourhood|neighborhood|blockout|greybox|room|rooms|interior|exterior|area|zone|hallway|corridor|building|house|apartment|tower|skyscraper|garage|workshop|warehouse|factory|office|station|airport|playground|park|garden|plaza|square|courtyard|market|shop|store|bar|cafe|restaurant|hotel|school|hospital|museum|arena|stadium|yard|dockyard|lounge|terrain|landscape|island|layout|circulation)\b/;

// a place is somewhere you stand, an object is something you pick up, and almost every decision about how
// to build one differs from the other, so both the router and the build stages ask this same question
export function names_a_place(text) {
  return scene_subject_pattern.test(normalized(text));
}

function bare_object_subject(value) {
  const match = value.match(
    /^\s*(?:please\s+)?(?:could\s+you\s+|can\s+you\s+|i\s+want\s+you\s+to\s+)?(?:create|make|build|generate|design|model)\s+(?:me\s+)?(?:a|an)\s+([a-z0-9][a-z0-9 _-]{0,60}?)(?=\s*$|[,.;]|\s+(?:with|that|which|featuring|made\s+of|using|from)\b)/,
  );
  if (!match?.[1])
  {
    return "";
  }

  const subject = match[1].trim();
  const asset_subject =
    strip_deliverable_words(subject);
  const head = asset_subject
    .split(/[\s_-]+/)
    .filter(Boolean)
    .pop() ?? "";
  if (
    subject.length < 3 ||
    names_a_place(head) ||
    /^(?:cube|sphere|quad|plane|cylinder|cone|camera|light|entity|component|mesh|primitive)$/.test(
      head,
    ) ||
    /\b(?:and|plus|along\s+with|together\s+with)\b/.test(value)
  )
  {
    return "";
  }
  if (
    /\b(?:onto|inside|next\s+to|beside|around|near|under|underneath|above|behind|in\s+front\s+of|scattered|arranged|placed|populate|fill)\b/.test(
      value,
    ) ||
    /\bon\s+(?:a|an|the)\b/.test(value)
  )
  {
    return "";
  }

  return subject;
}

function focused_asset_subject(value) {
  const explicit_3d =
    /\b(?:3d|asset|prefab|prop|mesh)\b/.test(value);
  if (
    !explicit_3d &&
    (
      /\b(?:data|domain|database|software|code|language|machine[\s-]+learning|ml|ai)\s+model\b/.test(
        value,
      ) ||
      /\b(?:database|schema|neural[\s-]+network|data[\s-]+structure|api|class|source[\s-]+code|simulation|financial|forecast|economic|statistical|mathematical|conceptual|business[\s-]+model)\b/.test(
        value,
      )
    )
  )
  {
    return "";
  }

  const explicit_patterns = [
    /\b(?:create|make|build|generate|design|model)\s+(?:me\s+)?(?:an?\s+)?(?:3d\s+)?(?:asset|model|prefab|prop)\s+(?:of|for)\s+(?:an?\s+|the\s+)?([a-z0-9][a-z0-9 _-]{0,60}?)(?=\s*$|[,.;]|\s+(?:with|that|which|featuring|made\s+of|using|from)\b)/,
    /\b(?:create|make|build|generate|design|model)\s+(?:me\s+)?(?:an?\s+)?(?:3d\s+)?(?:asset|model|prefab|prop)\s+(?:an?\s+|the\s+)?((?!(?:of|for|with|that|which|featuring|using|from)\b)[a-z0-9][a-z0-9 _-]{0,60}?)(?=\s*$|[,.;]|\s+(?:with|that|which|featuring|made\s+of|using|from)\b)/,
    /\b(?:create|make|build|generate|design|model)\s+(?:me\s+)?(?:an?\s+|the\s+)?([a-z0-9][a-z0-9 _-]{0,60}?)\s+(?:asset|model|prefab|prop)\b/,
  ];
  for (const pattern of explicit_patterns)
  {
    const match = value.match(pattern);
    if (!match?.[1])
    {
      continue;
    }

    const subject = match[1].trim();
    const head = subject
      .split(/[\s_-]+/)
      .filter(Boolean)
      .pop() ?? "";
    if (!names_a_place(head))
    {
      return subject;
    }
  }

  return bare_object_subject(value);
}

// words that say what kind of deliverable is wanted rather than what the thing is, a book asset is still
// a book and should not end up in the catalog called book_asset
const deliverable_word_pattern =
  /\b(?:asset|assets|prefab|prefabs|model|prop|props|mesh|object|item|reusable|standalone|isolated|focused|hero)\b/g;

function strip_deliverable_words(phrase) {
  let stripped = String(phrase ?? "")
    .replace(deliverable_word_pattern, " ")
    .replace(/\s+/g, " ")
    .trim();

  // removing the deliverable leaves the words that introduced it dangling, as in hardback book as a, and
  // they come off one at a time because there can be several
  let previous = "";
  while (stripped !== previous)
  {
    previous = stripped;
    stripped = stripped
      .replace(/\s+(?:as|for|to|in|of|into|a|an|the)$/, "")
      .trim();
  }

  // a request for nothing but the word asset still has to name something, so the original phrase stands
  return stripped.length > 0 ? stripped : String(phrase ?? "");
}

// verbs that continue work on something that already exists rather than starting something new
const revision_verb_pattern =
  /\b(revise|revisit|rework|redo|tweak|adjust|modify|change|alter|update|edit|iterate|improve|refine|polish|enhance|upgrade|fix|correct|continue)\b/;

// comparative and finish words, these are how a change to an existing thing actually gets phrased, as in
// make its glass thicker or make the label more worn
const revision_quality_pattern =
  /\b(thicker|thinner|taller|shorter|wider|narrower|longer|bigger|larger|smaller|rounder|smoother|sharper|softer|flatter|deeper|shallower|heavier|lighter|darker|brighter|glossier|shinier|rougher|cleaner|dirtier|worn|weathered|scratched|rusted|aged|crisper|denser|simpler|detailed|less|more)\b/;

function revision_aspects_from_prompt(value) {
  const aspects = [];
  if (
    /\b(geometry|mesh|shape|silhouette|form|profile|proportion|topology|thickness|thick|thicker|thinner|bevel|chamfer|fillet|radius|rim|seam|vertices|vertex|polygons?|tris|triangles|indices|lods?)\b/.test(
      value,
    )
  )
  {
    aspects.push("geometry");
  }
  if (
    /\b(material|materials|shader|roughness|metalness|metallic|gloss|glossy|matte|shiny|specular|reflective|reflection|transparen\w*|opaque|opacity|colour|color|tint|emissive)\b/.test(
      value,
    )
  )
  {
    aspects.push("material");
  }
  if (
    /\b(texture|textures|uv|uvs|mapping|map|maps|label|decal|print|sticker|logo|worn|wear|scratch\w*|dirt|grime|grunge|weather\w*|rust\w*|patina|stain\w*|pattern|tiling)\b/.test(
      value,
    )
  )
  {
    aspects.push("texture");
  }
  return aspects;
}

// the words naming which asset is being revised, the catalog decides later whether this actually names
// anything, so this only has to find the noun phrase the user pointed at
export function asset_hint_from_prompt(prompt) {
  const value = normalized(prompt);

  const patterns = [
    // asset called beer_bottle, prefab named x
    /\b(?:asset|prefab|model|prop)\s+(?:called|named|id)\s+["']?([a-z0-9][a-z0-9 _-]{0,50}?)["']?(?=[,.;]|\s+(?:and|so|then|to|with)\b|$)/,
    // the beer bottle asset, a beer bottle asset
    /\b(?:the|that|this|my|a|an)\s+([a-z0-9][a-z0-9 _-]{0,40}?)\s+(?:asset|prefab|model|prop)\b/,
    // the beer bottle's glass
    /\b(?:the|that|this|my)\s+([a-z0-9][a-z0-9 _-]{0,40}?)['’]s\b/,
    // revise the beer bottle, continue working on the beer bottle
    /\b(?:revise|revisit|rework|redo|tweak|adjust|modify|change|alter|update|edit|improve|refine|polish|enhance|upgrade|iterate\s+on|continue(?:\s+working)?(?:\s+on|\s+with)?)\s+(?:the\s+|my\s+|that\s+|this\s+)?([a-z0-9][a-z0-9 _-]{0,40}?)(?=[,.;]|\s+(?:so|to|and|then|by|with|for|make|but|geometry|mesh|material|texture)\b|$)/,
    // go to the beer bottle and ...
    /\b(?:go\s+to|open|load|pull\s+up)\s+(?:the\s+|my\s+)?([a-z0-9][a-z0-9 _-]{0,40}?)(?=[,.;]|\s+(?:and|then|so|to)\b|$)/,
  ];

  for (const pattern of patterns)
  {
    const match = value.match(pattern);
    if (!match?.[1])
    {
      continue;
    }

    // an aspect word is a part of the asset, not its name, and a word like existing says which one is
    // meant rather than what it is called, so the phrase is trimmed back to the subject
    const hint = match[1]
      .replace(
        /\b(geometry|mesh|meshes|material|materials|texture|textures|uv|uvs|mapping|shape|silhouette|colour|color|finish|surface|label|version|thing)\b/g,
        " ",
      )
      .replace(
        /^(?:existing|current|previous|last|earlier|same|new|old|my|the|that|this|an|a)\s+/g,
        "",
      )
      .replace(/\s+/g, " ")
      .trim();
    if (hint.length < 3 || scene_subject_pattern.test(hint))
    {
      continue;
    }

    return hint;
  }

  return "";
}

// a request to keep working on an asset that already exists, rather than to design a new one
//
// the router cannot see the catalog, so this only recognises the shape of the request. whether the named
// asset is really in the library is settled later, and a miss there falls back to the normal build path
function is_asset_revision_request(value) {
  const code_context =
    /\b(source|code|file|files|cpp|c\+\+|javascript|compile|build error|git|diff|commit|function|class)\b/.test(
      value,
    );
  if (code_context)
  {
    return false;
  }

  // a place is a scene even when the phrasing is identical, and a scene already has its own refinement path
  if (scene_subject_pattern.test(value))
  {
    return false;
  }

  const hint = asset_hint_from_prompt(value);
  const selected_asset_reference =
    revision_verb_pattern.test(value) &&
    (
      revision_aspects_from_prompt(value).length > 0 ||
      /\b(it|this|that|current|selected|front|back|cover|spine|pages?)\b/.test(
        value,
      )
    );
  if (!hint && !selected_asset_reference)
  {
    return false;
  }

  const names_the_library =
    /\b(asset|assets|prefab|library|catalog|catalogue)\b/.test(value);

  // engine level nouns are live scene edits with their own path, a cube is not a library asset. the
  // library words override this, an asset can legitimately be described as having a light or a collider
  if (
    !names_the_library &&
    /\b(cube|sphere|quad|plane|cylinder|cone|camera|light|lights|entity|entities|component|rigidbody|collider|primitive|spline|selection)\b/.test(
      value,
    )
  )
  {
    return false;
  }

  // a possessive presupposes its subject, the bottle's glass only means something if a bottle exists
  const possessive_reference =
    /\b(?:the|that|this|my)\s+[a-z0-9][a-z0-9 _-]{0,40}?['’]s\b/.test(value);
  const points_at_existing =
    /\b(existing|already|current|previous|last|earlier|that|this|its|it's)\b/.test(
      value,
    ) ||
    names_the_library ||
    possessive_reference;
  const aspects = revision_aspects_from_prompt(value);
  const wants_change =
    revision_verb_pattern.test(value) ||
    revision_quality_pattern.test(value) ||
    aspects.length > 0;
  if (!wants_change)
  {
    return false;
  }

  // starting a fresh design says create, and says nothing about an existing one
  const starts_new_build =
    /\b(create|make|build|generate|construct|design|model)\s+(?:me\s+)?(?:a|an)\s+/.test(
      value,
    ) &&
    !points_at_existing &&
    !revision_verb_pattern.test(value);
  if (starts_new_build)
  {
    return false;
  }

  // an explicit revision verb is signal enough on its own, otherwise the request has to both point at
  // something existing and say what about it should differ, which is what make its glass thicker does
  return (
    revision_verb_pattern.test(value) ||
    names_the_library ||
    (
      points_at_existing &&
      (
        aspects.length > 0 ||
        revision_quality_pattern.test(value)
      )
    )
  );
}

function is_scene_refinement_request(value) {
  const starts_new_build =
    /^(?:create|make|build|generate|construct|blockout|design)\b/.test(
      value,
    ) &&
    !/\b(?:existing|selected|current|this)\s+(?:scene|environment|entity|area|level|map)\b/.test(
      value,
    );
  if (starts_new_build)
  {
    return false;
  }

  const refines =
    /\b(improve|refine|polish|enhance|upgrade|detail|beautify|finish|complete|correct)\b/.test(value);
  const references_live_scene =
    /\b(it|this|current|existing|scene|selection|selected|environment|area|level|map)\b/.test(value) ||
    target_name_from_prompt(value) !== "";
  const code_context =
    /\b(source|code|file|files|cpp|c\+\+|javascript|compile|build error|git|diff)\b/.test(value);
  return refines && references_live_scene && !code_context;
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
  const creates_environment =
    /\b(create|make|build|generate|construct|design|layout|lay out|place|add)\b/.test(value) &&
    /\b(scene|world|environment|area|level|map|room|building|airport|playground|park|factory|warehouse|station|street|plaza|office|house|landscape|arena|yard|garden)\b/.test(value);
  if (creates_environment)
  {
    return false;
  }

  const mentions_lights = /\blights?\b/.test(value);
  const wants_calibrate = /\b(calibrat\w*|fix|tune|adjust|boost|strengthen|make (?:them |the lights )?visible)\b/.test(value);
  const all_or_scene = /\b(all|every|scene|world|existing)\b/.test(value) || /\bbased on (?:their|its) nature\b/.test(value);
  return mentions_lights && wants_calibrate && (all_or_scene || /\bnature\b/.test(value));
}

function is_city_develop_request(value) {
  const positive_value = value.replace(
    /\bnot\b[^.!?]{0,80}\b(?:city|district|downtown|urban)\b/g,
    "",
  );
  const standalone_environment_but_not_city =
    /\b(airport|playground|factory|warehouse|station|office|house|arena|yard|garden|garage|workshop|lounge|bar)\b/.test(
      positive_value,
    ) &&
    !/\b(city|district|downtown|residential|industrial|urban)\b/.test(
      positive_value,
    );
  if (standalone_environment_but_not_city)
  {
    return false;
  }

  const wants_city = /\b(road|roads|spline|highway|street|connect|link|network|district|landmark|city|blockout|market|downtown|skyscraper|park|industrial|residential|plaza|parking)\b/.test(positive_value);
  const constructive = /\b(create|make|build|connect|link|lay|add|generate|scan|decorate|develop|block\s*out|plan|continue|finish|polish|improve|refine|enhance|upgrade|detail|beautify)\b/.test(positive_value);
  const has_targets = /\b(gas_station|dockyard|airway|airport|playground|landmark|between|from|to|map|world|areas?|districts?|city|around)\b/.test(positive_value) || /\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+\b/.test(positive_value);
  return wants_city && constructive && has_targets && !/\b(source|code|file|cpp|c\+\+|javascript)\b/.test(positive_value);
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

  // ahead of the scene paths, they read a change to a named thing as a scene refinement and would rebuild
  // the asset from nothing instead of continuing it
  if (is_asset_revision_request(value))
  {
    const asset_hint = asset_hint_from_prompt(prompt);
    return {
      kind: "asset_revise",
      confidence: 0.86,
      live_scene_action: true,
      allow_cursor_fallback: true,
      asset_hint,
      target_name: clean_target_name(asset_hint),
      revision_aspects: revision_aspects_from_prompt(value),
      use_selected: asset_hint.length === 0,
    };
  }

  if (is_city_develop_request(value))
  {
    const use_selected =
      should_use_selected_entity(prompt);
    return {
      kind: "city_develop",
      confidence: 0.92,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: use_selected
        ? ""
        : scene_root_name_from_prompt(prompt),
      use_selected,
      landmarks: landmarks_from_prompt(prompt),
    };
  }

  if (is_scene_refinement_request(value))
  {
    const target_name = target_name_from_prompt(prompt);
    return {
      kind: "scene_rebuild",
      confidence: 0.9,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name,
      use_selected: target_name === "",
    };
  }

  if (is_rebuild_scene_request(value))
  {
    return {
      kind: "scene_rebuild",
      confidence: 0.9,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: should_use_selected_entity(prompt)
        ? ""
        : scene_root_name_from_prompt(prompt),
      use_selected: should_use_selected_entity(prompt),
    };
  }

  if (is_terse && is_source_code_request(value)) {
    return { kind: "source_code", confidence: 0.9 };
  }

  const focused_asset_subject_name =
    focused_asset_subject(value);
  if (focused_asset_subject_name)
  {
    return {
      kind: "focused_asset",
      confidence: 0.9,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: clean_target_name(
        strip_deliverable_words(
          focused_asset_subject_name,
        ),
      ),
      use_selected: false,
    };
  }

  if (is_scene_construction_request(value))
  {
    return {
      kind: "scene_rebuild",
      confidence: 0.88,
      live_scene_action: true,
      allow_cursor_fallback: true,
      target_name: should_use_selected_entity(prompt)
        ? ""
        : scene_root_name_from_prompt(prompt),
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
