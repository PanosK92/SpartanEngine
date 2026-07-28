// decides, before any geometry exists, which props a scene needs and which of them the library can
// already supply
//
// a blockout made of cubes stays a blockout. the way it stops being one is that the agent asks the
// library first every time it is about to place a recognisable object, and only falls back to boxes for
// the things the library has never seen. the agent was already told to do that in prose and did not, so
// this does the asking up front and hands it the answer as a list it cannot miss

import { world_asset_catalog_entries } from "./world_asset_catalog.mjs";

// words that name a room or a region rather than a thing that can be placed in one, an inventory full
// of these produces searches that can never match a prop
const NOT_A_PROP = new Set([
  "floor", "ceiling", "wall", "walls", "roof", "ground", "terrain", "sky",
  "room", "shell", "zone", "area", "space", "region", "layout", "circulation",
  "path", "route", "entrance", "exit", "corridor", "aisle", "clearance",
  // lighting is the brief's heading for the subject, a light on its own is a fixture and a real prop
  "lighting", "ambience", "atmosphere", "mood",
  "structure", "foundation", "framework", "massing", "district", "block",
  "material", "materials", "texture", "textures", "finish", "palette",
  "detail", "details", "wear", "scale", "proportion", "silhouette",
]);

// a small synonym net so the vocabulary the brief uses can reach the vocabulary the library was named
// with, this is what lets "table" find a workbench and "tv" find a television
// bench is deliberately in neither the table nor the chair group. it means both a work surface and a
// seat, so listing it in either one bridges them, and a request for a stool comes back with a workbench
const SYNONYMS = [
  ["table", "desk", "workbench", "counter", "worktop", "tabletop"],
  ["chair", "stool", "seat", "armchair"],
  ["sofa", "couch", "settee"],
  ["cupboard", "cabinet", "locker", "wardrobe", "closet", "press"],
  ["shelf", "shelves", "shelving", "rack", "bookcase", "bookshelf"],
  ["tv", "television", "monitor", "screen", "display"],
  ["lamp", "light", "lantern", "luminaire", "sconce", "bulb"],
  ["bin", "trashcan", "dustbin", "wastebasket", "garbage"],
  ["box", "crate", "carton", "case"],
  ["barrel", "drum", "keg", "cask"],
  ["bottle", "flask", "jar", "vial"],
  ["cup", "mug", "glass", "tumbler"],
  ["book", "novel", "tome", "manual", "magazine"],
  ["car", "vehicle", "automobile", "sedan"],
  ["toolbox", "toolchest", "toolcase"],
  ["tyre", "tire", "wheel"],
  ["sign", "signage", "signboard", "placard", "board"],
  ["pipe", "tube", "conduit", "duct"],
  ["fridge", "refrigerator", "freezer", "cooler"],
  ["oven", "stove", "cooker", "hob", "range"],
  ["sink", "basin", "washbasin"],
  ["bed", "bunk", "cot", "mattress"],
  ["door", "doorway", "hatch", "gate"],
  ["window", "pane", "glazing"],
  ["plant", "pot", "planter", "shrub", "bush"],
  ["rug", "carpet", "mat"],
  ["picture", "painting", "poster", "frame", "artwork"],
  ["computer", "pc", "laptop", "workstation"],
  ["ladder", "stepladder", "steps"],
  ["cart", "trolley", "dolly", "wagon"],
];

// head nouns that carry no subject of their own, "shelf unit" is a shelf and "tool set" is a tool, so
// the word before them is what the request is really about
const GENERIC_HEADS = new Set([
  "unit", "set", "piece", "item", "object", "thing", "prop", "model",
  "assembly", "group", "arrangement", "collection", "kit",
]);

const MAX_ITEMS = 24;
const MAX_CANDIDATES_PER_ITEM = 3;

// the minimum score worth showing the agent, below this the match is a coincidence and suggesting it
// would be worse than saying nothing
const SCORE_FLOOR = 3;

function normalise(value) {
  return String(value ?? "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

// a crude singulariser, the library and the brief disagree about plurals constantly and a stemmer would
// be a dependency for the sake of one letter
function singular(word) {
  if (word.length > 4 && word.endsWith("ies")) {
    return `${word.slice(0, -3)}y`;
  }
  if (word.length > 3 && word.endsWith("ses")) {
    return word.slice(0, -2);
  }
  if (word.length > 3 && word.endsWith("s") && !word.endsWith("ss")) {
    return word.slice(0, -1);
  }
  return word;
}

function words_of(value) {
  return normalise(value)
    .split(" ")
    .filter(Boolean)
    .map(singular);
}

// the noun a phrase is actually about, which is its last word unless that word is a generic head
function subject_of(words) {
  if (words.length === 0) {
    return "";
  }

  const last = words[words.length - 1];
  if (words.length > 1 && GENERIC_HEADS.has(last)) {
    return words[words.length - 2];
  }
  return last;
}

// every word that should be treated as meaning the same thing as the given one
function expand(word) {
  const expanded = new Set([word]);
  for (const group of SYNONYMS) {
    if (group.includes(word)) {
      for (const member of group) {
        expanded.add(member);
      }
    }
  }
  return expanded;
}

// the brief is asked to end a scene with a plain inventory line, that line is the list of things the
// scene is actually made of, which is the list worth searching for
export function inventory_from_brief(brief) {
  const value = String(brief ?? "");
  const match = value.match(
    /^[ \t]*inventory[ \t]*:[ \t]*(.+(?:\n(?![ \t]*[a-z ]{3,30}:).*)*)/im,
  );
  if (!match) {
    return [];
  }

  return dedupe_items(
    match[1]
      .split(/[,;\n]/)
      .map((entry) =>
        normalise(entry.replace(/^[\s\-*\d.)]+/, "")),
      ),
  );
}

// plan elements are named for their role rather than their subject, so most are not props, the few that
// are still worth searching for
export function inventory_from_plan(plan) {
  const elements = plan?.elements ?? [];
  return dedupe_items(
    elements.map((element) => normalise(element?.name ?? element?.role ?? "")),
  );
}

function dedupe_items(candidates) {
  const items = [];
  const seen = new Set();
  for (const candidate of candidates) {
    const item = candidate.trim();
    if (item.length < 3 || item.length > 40) {
      continue;
    }

    const parts = words_of(item);
    if (parts.length === 0 || parts.length > 4) {
      continue;
    }
    // an entry made only of room and surface words describes the container, not its contents
    if (parts.every((part) => NOT_A_PROP.has(part))) {
      continue;
    }
    if (seen.has(item)) {
      continue;
    }
    seen.add(item);
    items.push(item);
    if (items.length >= MAX_ITEMS) {
      break;
    }
  }
  return items;
}

// how well one library entry answers one wanted item
//
// the subject has to be found in what the asset IS, meaning its id, name or aliases, never in its tags
// alone. a bottle carries tags like shelf and kitchen because that is where it belongs, and a tag match
// would offer that bottle up as a shelf. tags only ever add supporting weight to a subject already
// recognised, and a synonym counts for less than the word itself
export function score_asset(item, asset) {
  const wanted = words_of(item).filter(
    (word) => !NOT_A_PROP.has(word),
  );
  if (wanted.length === 0) {
    return { score: 0, matched: [] };
  }

  // every name the asset goes by, each contributing all of its words for scoring and its own subject
  // for the gate below
  const names = [
    words_of(asset.id),
    words_of(asset.name),
    ...(asset.aliases ?? []).map(words_of),
  ].filter((words) => words.length > 0);

  const identity = new Set(names.flat());
  const identity_subjects = new Set(names.map(subject_of));
  const descriptive = new Set([
    ...(asset.tags ?? []).flatMap(words_of),
    ...(asset.constraints?.style ?? []).flatMap(words_of),
    ...(asset.constraints?.materials ?? []).flatMap(words_of),
  ]);

  const weigh = (word) => {
    let best = 0;
    for (const candidate of expand(word)) {
      const exact = candidate === word;
      if (identity.has(candidate)) {
        best = Math.max(best, exact ? 6 : 4);
      } else if (descriptive.has(candidate)) {
        best = Math.max(best, exact ? 3 : 2);
      }
    }
    return best;
  };

  // the wanted subject has to be what the asset IS, not merely a word appearing in its name. a stacked
  // car tyre contains the word car and is not a car, so matching any word would answer a request for a
  // car with a pile of tyres
  const subject = subject_of(wanted);
  const subject_matches = [...expand(subject)].some(
    (candidate) => identity_subjects.has(candidate),
  );
  if (!subject_matches) {
    return { score: 0, matched: [] };
  }

  let score = 0;
  const matched = [];
  for (const word of wanted) {
    const weight = weigh(word);
    if (weight > 0) {
      score += weight;
      matched.push(word);
    }
  }

  // matching every word asked for is meaningfully better than matching only the subject
  if (matched.length === wanted.length) {
    score += 2;
  }
  return { score, matched };
}

// finds the one library asset a request is pointing at by name
//
// this is the other direction to build_reuse_plan. that one asks what could stand in for a thing the scene
// needs and is happy with a near miss, this one is deciding whether the thing the user named exists at all,
// so it reports the runner up too and lets the caller refuse to guess when two candidates are level
export async function resolve_asset_by_name({
  project_root,
  resource_directory,
  hint,
  entries: provided_entries = null,
}) {
  const wanted = String(hint ?? "").trim();
  if (wanted.length === 0) {
    return { ok: false, reason: "no asset name in the request" };
  }

  let entries = provided_entries;
  if (!entries) {
    try {
      entries = await world_asset_catalog_entries(
        project_root,
        resource_directory,
      );
    } catch (error) {
      return {
        ok: false,
        reason: `could not read the asset catalog: ${error.message}`,
      };
    }
  }
  if (entries.length === 0) {
    return { ok: false, reason: "the asset library is empty" };
  }

  const scored = entries
    .map((asset) => ({ asset, ...score_asset(wanted, asset) }))
    .filter((candidate) => candidate.score >= SCORE_FLOOR)
    .sort((left, right) => right.score - left.score);

  if (scored.length === 0) {
    return {
      ok: false,
      reason: `nothing in the library matches "${wanted}"`,
      library_size: entries.length,
    };
  }

  // an exact id or name match settles it outright, otherwise a tie between two different assets is a real
  // ambiguity and picking one of them silently would edit the wrong asset
  const normalised_wanted = normalise(wanted);
  const exact = scored.find(
    (candidate) =>
      normalise(candidate.asset.id) === normalised_wanted ||
      normalise(candidate.asset.name) === normalised_wanted,
  );
  if (!exact && scored.length > 1 && scored[0].score === scored[1].score) {
    return {
      ok: false,
      reason: `"${wanted}" matches several library assets equally well`,
      ambiguous: scored.slice(0, 3).map((candidate) => candidate.asset.id),
      library_size: entries.length,
    };
  }

  const chosen = exact ?? scored[0];
  return {
    ok: true,
    asset: chosen.asset,
    score: chosen.score,
    matched_on: chosen.matched,
    exact: Boolean(exact),
    alternatives: scored
      .filter((candidate) => candidate.asset.id !== chosen.asset.id)
      .slice(0, 3)
      .map((candidate) => candidate.asset.id),
    library_size: entries.length,
  };
}

// pairs the things the scene needs against the things the library has, the answer is two lists, what to
// load and what still has to be built
export async function build_reuse_plan({
  project_root,
  resource_directory,
  items,
  entries: provided_entries = null,
}) {
  if (items.length === 0) {
    return { reuse: [], missing: [], library_size: 0 };
  }

  let entries = provided_entries ?? [];
  if (!provided_entries) {
    try {
      entries = await world_asset_catalog_entries(
        project_root,
        resource_directory,
      );
    } catch {
      return { reuse: [], missing: [], library_size: 0 };
    }
  }

  const reuse = [];
  const missing = [];
  for (const item of items) {
    const scored = entries
      .map((asset) => ({ asset, ...score_asset(item, asset) }))
      .filter((candidate) => candidate.score >= SCORE_FLOOR)
      .sort((left, right) => right.score - left.score)
      .slice(0, MAX_CANDIDATES_PER_ITEM);

    if (scored.length === 0) {
      missing.push(item);
      continue;
    }

    reuse.push({
      wanted: item,
      candidates: scored.map((candidate) => ({
        asset_id: candidate.asset.id,
        name: candidate.asset.name,
        type: candidate.asset.type,
        version: candidate.asset.active_version,
        tags: candidate.asset.tags ?? [],
        matched_on: candidate.matched,
      })),
    });
  }

  return { reuse, missing, library_size: entries.length };
}

export function reuse_prompt_lines(plan) {
  if (!plan || (plan.reuse.length === 0 && plan.missing.length === 0)) {
    return [];
  }

  const lines = [
    "Asset library check, already performed for this request:",
  ];

  if (plan.reuse.length > 0) {
    lines.push(
      "These objects already exist in the library as promoted assets. Load them with world_asset_load instead of approximating them with primitives, then position each one with entity_set_transform or entity_set_transform_batch. Confirm the candidate actually is the object you need before using it, and prefer the first candidate when several fit.",
      JSON.stringify(plan.reuse),
    );
  }

  if (plan.missing.length > 0) {
    lines.push(
      `The library has nothing for these, so build them yourself: ${plan.missing.join(", ")}.`,
    );
  }

  lines.push(
    "That check covered the objects known before the build started. Whenever you decide to place a recognisable object that is not in the lists above, call world_asset_search for it first and reuse a promoted match if one exists. Only fall back to primitives once the library has been asked and has nothing.",
  );

  if (plan.library_size === 0) {
    lines.push(
      "The library is currently empty, so everything in this scene has to be built. Register the reusable objects you build so later scenes can load them instead of rebuilding them.",
    );
  }

  return lines;
}
