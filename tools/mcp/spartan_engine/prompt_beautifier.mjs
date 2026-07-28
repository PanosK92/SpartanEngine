// expands a terse request into a design brief before the build starts
//
// a short prompt like "make a beer bottle" carries a complete picture in the author's head and almost
// none of it on the wire, so the build guesses. this turns the request into an explicit brief covering
// the things a modeller would ask about, and hands that to the build alongside the original words. the
// original is never replaced, the brief is additive, so a detailed prompt loses nothing by passing
// through here

import { Agent } from "@cursor/sdk";
import { fileURLToPath } from "node:url";
import path from "node:path";

const __dirname = path.dirname(
  fileURLToPath(import.meta.url),
);

// this runs only after the deterministic paths have already declined the request, so the remaining
// intents are nearly all builds, only the ones that clearly end in reading rather than geometry are
// worth excluding by name
const NON_BUILD_INTENTS = new Set([
  "none",
  "simple_read",
  "mcp_status",
  "console_read",
  "engine_mode",
  "source_code",
  "delete_entity",
  "delete_children",
  // a revision already has a subject that exists, expanding it into a full specification for the whole
  // object invites a rebuild to match the brief instead of the change that was asked for
  "asset_revise",
]);

// wording that says the user wants it rough, expanding these would fight the request. speed words like
// quick are deliberately absent, they describe how long the user will wait, not how crude they want it
const DELIBERATELY_ROUGH =
  /\b(?:blockout|block\s?out|greybox|grey\s?box|graybox|gray\s?box|proxy|placeholder|low[\s-]?poly|rough|crude|simple|basic|minimal|stub)\b/i;

// wording that says the user already wrote the brief themselves
const ALREADY_DETAILED =
  /\b(?:millimet|centimet\w*|\d+\s?(?:mm|cm|m)\b|thickness|bevel|chamfer|fillet|radius|silhouette|topology|ridge|flute|knurl|embossed|debossed|anodiz|brushed|matte|glossy|specular|roughness|metalness|normal map|uv|tiling)\b/i;

// subjects that are a place rather than an object, a place brief talks about zones, circulation and
// lighting, an object brief talks about construction, thickness and material splits
const PLACE_SUBJECTS =
  /\b(?:scene|level|map|world|environment|city|town|village|district|neighbourhood|neighborhood|street|road|alley|avenue|square|plaza|courtyard|park|garden|forest|landscape|terrain|island|beach|desert|room|interior|exterior|hall|corridor|lobby|kitchen|bathroom|bedroom|office|warehouse|factory|workshop|garage|hangar|station|terminal|airport|dockyard|harbour|harbor|port|market|shop|store|bar|pub|cafe|restaurant|library|museum|church|temple|castle|fort|camp|base|arena|stadium|track|circuit|bridge|tunnel|ruins|settlement|building|house|apartment|tower|skyscraper|farm|mine|quarry|graveyard|cemetery)\b/i;

// questions and read only requests, there is nothing to design
const NOT_A_BUILD =
  /^\s*(?:what|why|how|when|where|which|who|is|are|does|do|did|can|could|should|would|will|list|show|tell|explain|describe|find|search|read|open|select|focus|look|cancel|stop|undo|redo|save|load|delete|remove|clean|clear)\b/i;

const MAX_BRIEF_LENGTH = 4000;
const DEFAULT_TIMEOUT_MS = 60000;

// a prompt this long is already carrying its own detail
const LONG_PROMPT_WORDS = 60;

function word_count(text) {
  const value = String(text ?? "").trim();
  if (value.length === 0) {
    return 0;
  }
  return value.split(/\s+/).length;
}

// the intent router calls any create verb a scene rebuild, so "make a beer bottle" arrives labelled a
// scene, the subject decides the brief shape instead, a place gets zones and lighting, an object gets
// construction and thickness
function is_place_request(prompt) {
  return PLACE_SUBJECTS.test(String(prompt ?? "").toLowerCase());
}

// reports why enrichment was or was not applied, the caller surfaces this so the behaviour is never a
// silent mystery when a prompt comes back plainer than expected
export function should_beautify(prompt, intent) {
  const value = String(prompt ?? "").trim();
  if (value.length === 0) {
    return { ok: false, reason: "empty prompt" };
  }

  const words = word_count(value);
  if (words >= LONG_PROMPT_WORDS) {
    return {
      ok: false,
      reason: `prompt already carries ${words} words of detail`,
    };
  }
  if (ALREADY_DETAILED.test(value)) {
    return {
      ok: false,
      reason: "prompt already specifies dimensions, finish or topology",
    };
  }
  // "block out a garage" asks for a garage, laid out. for a place, blockout names the stage of the work
  // rather than the standard expected of it, and the inventory the brief produces is what lets the
  // library be checked for real props instead of the whole thing becoming boxes. for a single object the
  // same word does mean keep it crude, so the skip still applies there
  if (DELIBERATELY_ROUGH.test(value) && !is_place_request(value)) {
    return {
      ok: false,
      reason: "prompt asks for a rough or low detail result",
    };
  }
  if (NOT_A_BUILD.test(value)) {
    return { ok: false, reason: "prompt is a question or a read only request" };
  }

  const kind = String(intent?.kind ?? "cursor");
  if (NON_BUILD_INTENTS.has(kind)) {
    return { ok: false, reason: `intent ${kind} does not build geometry` };
  }

  return { ok: true, reason: `terse ${words} word build request` };
}

function brief_instructions(prompt, intent) {
  const scene =
    intent?.kind === "city_develop" ||
    is_place_request(prompt);

  const lines = [
    "You are a senior art director writing a build brief for a 3D artist working in a game engine.",
    "",
    "The request below is deliberately terse. Expand it into the brief the author would have written if they had the patience, describing what an experienced eye would expect from this subject by default. Stay faithful to the request: add the detail it implies, never a different object and never features that contradict it.",
    "",
    "Rules:",
    "- Reply with the brief only. No preamble, no closing remarks, no markdown headings, no code fences.",
    "- Do not use any tools and do not read any files. Answer from knowledge.",
    "- Be concrete and physical. Real proportions in metres, real thicknesses, real materials.",
    "- Where a real reference exists, describe the ordinary version of it rather than an exotic one.",
    "- State only what a modeller can act on. Skip mood, story, marketing language and adjectives that carry no geometry.",
    "- The headings below are prompts to think about, not a form to fill in. Cover the ones this subject actually has, drop the ones it does not, and add a heading of your own where the subject needs one that is not listed.",
    "- Keep it under 300 words.",
  ];

  if (!scene) {
    // the brief is read as a build list, so anything it names as a feature gets modelled. asking it for
    // seams, embossing and text is how a living room television acquired geometry for its hdmi ports, its
    // regulatory markings and its screw recesses
    lines.push(
      "- This is an environment prop for a video game, seen from across a room, not a hero asset and not a render. Describe the ordinary, plain version of the subject at the level of detail that reads at that distance. Do not describe a premium, flagship or feature-laden variant unless the request asked for one.",
      "- Keep the complete asset near 6000 triangles, no more than 12 authored parts and no more than 4 reused materials. Only the explicit phrase hero asset or hero quality may raise those limits.",
      "- Name only features that change the outline or the material. Do not list screws, fasteners, ports, sockets, connectors, cables, vents, grilles, perforations, panel seams, embossed or printed text, badges, logos, regulatory markings, or internal components. Those are painted into the textures, so putting them in the brief only invites wasted geometry.",
      "- Describe the faces that get looked at. A subject that stands against a wall or sits on the floor has a back or an underside that is a plain panel, so say so in one clause and spend the brief on the front.",
    );
  }

  if (scene) {
    lines.push(
      "",
      "Cover, as short labelled lines:",
      "Overall footprint and scale in metres.",
      "Zones and how someone moves through the space.",
      "Structure, the surfaces that enclose and support it.",
      "Function, the objects that explain how the place is used.",
      "Materials and finishes for the main surfaces.",
      "Lighting, the sources and the time of day they imply.",
      "Detail and wear that make it look lived in.",
      "",
      // this line is parsed, not just read, it is what gets looked up in the asset library before the
      // build starts, so it has to be plain names rather than prose
      "Then end with a line that starts exactly with \"Inventory:\" followed by a comma separated list of every discrete object a person could pick up, move or walk around in this place. Use plain lowercase singular names a modeller would recognise, such as workbench, tyre, toolbox, ceiling lamp. Name the objects only. Do not include floors, walls, ceilings, zones, routes or materials, and do not add counts, sizes or descriptions.",
    );
  } else {
    lines.push(
      "",
      "Cover, as short labelled lines:",
      "Overall dimensions in metres and the silhouette.",
      "Primary form, the main body and the profile that defines it.",
      "Secondary construction, only the few parts that visibly change the silhouette, function or material boundary. Combine the rest.",
      "Thickness, and how edges, openings, joins and contact surfaces are actually formed. A hollow subject has walls and rims, a solid one has edge treatments, a layered one has a stack. Describe whichever this subject has.",
      "Moving or posable parts only when the request requires them or they define the normal gameplay silhouette.",
      "A small reused material set, no more than four materials for the complete prop.",
      "Surface character that belongs in the textures rather than the geometry, such as grain, weave, print or wear. Name it as texture work so it is not modelled.",
      "Do not invent extra pieces or material splits for details that can be baked into textures.",
    );
  }

  lines.push(
    "",
    "Request:",
    String(prompt ?? "").trim(),
  );

  return lines.join("\n");
}

function text_from_turn(turn) {
  if (typeof turn === "string") {
    return turn;
  }
  if (typeof turn?.text === "string") {
    return turn.text;
  }

  const content = turn?.message?.content ?? turn?.content;
  if (typeof content === "string") {
    return content;
  }
  if (Array.isArray(content)) {
    return content
      .map((block) =>
        typeof block === "string" ? block : String(block?.text ?? ""),
      )
      .join("\n");
  }
  return "";
}

// the run result holds the final answer, the conversation is the fallback for a run that streamed its
// answer without populating result
async function collect_run_text(run) {
  const result = await run.wait();
  const text = String(result?.result ?? "").trim();
  if (text.length > 0) {
    return text;
  }

  if (!run?.supports?.("conversation")) {
    return "";
  }

  try {
    const conversation = await run.conversation();
    for (let index = conversation.length - 1; index >= 0; index--) {
      const turn = text_from_turn(conversation[index]).trim();
      if (turn.length > 0) {
        return turn;
      }
    }
  } catch {
  }
  return "";
}

// the model is asked for plain prose, but a model asked for plain prose still sometimes wraps it, so
// the obvious wrappers come off rather than being passed downstream as literal syntax
function clean_brief(text) {
  let value = String(text ?? "").trim();
  if (value.startsWith("```")) {
    value = value
      .replace(/^```[a-z]*\s*/i, "")
      .replace(/```\s*$/, "")
      .trim();
  }
  value = value
    .split("\n")
    .map((line) => line.replace(/^\s*#{1,6}\s+/, ""))
    .join("\n")
    .trim();

  if (value.length > MAX_BRIEF_LENGTH) {
    value = `${value.slice(0, MAX_BRIEF_LENGTH).trimEnd()}...`;
  }
  return value;
}

export async function beautify_prompt({
  prompt,
  intent = null,
  api_key = "",
  model_id = "auto",
  timeout_ms = DEFAULT_TIMEOUT_MS,
  on_note = null,
}) {
  const decision = should_beautify(prompt, intent);
  if (!decision.ok) {
    return { ok: false, skipped: true, reason: decision.reason, brief: "" };
  }
  if (!api_key) {
    return {
      ok: false,
      skipped: true,
      reason: "no cursor api key available for enrichment",
      brief: "",
    };
  }

  // a throwaway agent with no custom tools, plan mode cannot mutate the engine, so a wandering
  // enrichment pass can never touch the world the build is about to work on
  let agent = null;
  try {
    on_note?.("expanding the request into a design brief");
    agent = await Agent.create({
      apiKey: api_key,
      model: { id: model_id },
      mode: "plan",
      local: {
        cwd: __dirname,
        settingSources: [],
      },
    });

    const run = await agent.send(
      brief_instructions(prompt, intent),
    );

    // enrichment is optional work in front of the real build, so it gets a deadline rather than the
    // right to stall the run, and the abandoned run is cancelled so it stops costing anything
    let timer = null;
    const text = await Promise.race([
      collect_run_text(run),
      new Promise((resolve) => {
        timer = setTimeout(
          () => resolve(""),
          timeout_ms,
        );
        timer.unref?.();
      }),
    ]);
    if (timer) {
      clearTimeout(timer);
    }
    if (text.length === 0 && run?.supports?.("cancel")) {
      void run.cancel().catch(() => {});
    }

    const brief = clean_brief(text);
    if (brief.length < 40) {
      return {
        ok: false,
        skipped: false,
        reason: "enrichment returned nothing usable",
        brief: "",
      };
    }

    return {
      ok: true,
      skipped: false,
      reason: decision.reason,
      brief,
    };
  } catch (error) {
    return {
      ok: false,
      skipped: false,
      reason: `enrichment failed: ${error.message}`,
      brief: "",
    };
  } finally {
    if (agent?.[Symbol.asyncDispose]) {
      await agent[Symbol.asyncDispose]().catch(() => {});
    } else if (agent?.close) {
      try {
        agent.close();
      } catch {
      }
    }
  }
}
