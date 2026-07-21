const camera_views = {
  perspective: {
    id: "perspective",
    view: "perspective",
    target: "scene_root",
    padding: 1.18,
    purpose: "overall composition and depth",
  },
  front: {
    id: "front",
    view: "front",
    target: "scene_root",
    padding: 1.12,
    purpose: "facade readability and alignment",
  },
  side: {
    id: "side",
    view: "right",
    target: "scene_root",
    padding: 1.12,
    purpose: "depth, support, and silhouette",
  },
  top: {
    id: "top",
    view: "top",
    target: "scene_root",
    padding: 1.08,
    purpose: "layout, spacing, and circulation",
  },
  detail: {
    id: "detail",
    view: "perspective",
    target: "detail_anchor",
    padding: 1.35,
    purpose: "materials and small geometry",
  },
};

const human_rubric = {
  prompt_fidelity: {
    label: "Prompt fidelity",
    description: "The scene includes the requested elements and intent.",
  },
  composition: {
    label: "Composition",
    description: "The layout has a clear hierarchy and reads from fixed views.",
  },
  scale_and_proportion: {
    label: "Scale and proportion",
    description: "Dimensions and relationships appear credible.",
  },
  geometry_quality: {
    label: "Geometry quality",
    description: "Silhouettes, supports, and details avoid primitive repetition.",
  },
  material_quality: {
    label: "Material quality",
    description: "Materials are varied, coordinated, and semantically appropriate.",
  },
  lighting_quality: {
    label: "Lighting quality",
    description: "Lighting supports readability, mood, and focal hierarchy.",
  },
};

const score_weights = {
  plan_fidelity: 0.25,
  duplicate_control: 0.1,
  material_coverage: 0.1,
  audit_pass: 0.15,
  correction_efficiency: 0.05,
  human_score: 0.25,
  visual_score: 0.1,
};

const visual_metric_names = [
  "composition_balance",
  "silhouette_readability",
  "lighting_contrast",
  "material_separation",
  "framing_coverage",
];

function create_benchmark(
  id,
  category,
  title,
  prompt,
  expected_features,
  camera_view_ids,
  scoring = {},
) {
  return {
    id,
    category,
    title,
    prompt,
    expected_features,
    camera_view_ids,
    scoring: {
      max_duplicate_count: 2,
      max_default_material_ratio: 0.2,
      preferred_correction_count: 1,
      max_correction_count: 3,
      pass_score: 75,
      min_human_average: 3.5,
      min_visual_score: 0.65,
      ...scoring,
    },
  };
}

const built_in_benchmarks = [
  create_benchmark(
    "room_living_day",
    "room",
    "Daylit living room",
    "Create a finished contemporary living room with a sofa grouping, coffee table, media wall, shelving, rug, windows, and warm daylight. Use credible residential scale, layered materials, and small decorative details.",
    [
      "sofa grouping",
      "coffee table",
      "media wall",
      "shelving",
      "rug",
      "daylight",
    ],
    ["perspective", "top", "detail"],
  ),
  create_benchmark(
    "room_workshop_night",
    "room",
    "Night workshop",
    "Create a compact mechanical workshop at night with a workbench, wall tools, storage cabinets, task lamps, overhead fixtures, floor wear, and a clear working aisle. Use metal, painted wood, rubber, and concrete materials.",
    [
      "workbench",
      "wall tools",
      "storage",
      "task lighting",
      "working aisle",
      "surface wear",
    ],
    ["perspective", "front", "top"],
  ),
  create_benchmark(
    "storefront_bakery",
    "storefront",
    "Corner bakery storefront",
    "Create a welcoming corner bakery storefront with framed display windows, an inset entrance, awning, readable sign structure, product displays, sidewalk planters, facade trim, and warm interior lighting.",
    [
      "display windows",
      "inset entrance",
      "awning",
      "sign",
      "product displays",
      "planters",
    ],
    ["perspective", "front", "detail"],
  ),
  create_benchmark(
    "storefront_neon_market",
    "storefront",
    "Neon night market",
    "Create a small urban convenience market at night with a glazed facade, sliding entrance, canopy, layered neon signage, exterior vending area, curb details, and wet reflective pavement. Keep the interior readable through the glass.",
    [
      "glazed facade",
      "sliding entrance",
      "canopy",
      "neon signage",
      "vending area",
      "wet pavement",
    ],
    ["perspective", "front", "side"],
    {
      max_default_material_ratio: 0.12,
    },
  ),
  create_benchmark(
    "gas_station_highway_dusk",
    "gas_station",
    "Highway gas station",
    "Create a complete roadside gas station at dusk with a convenience building, large supported canopy, four distinct pumps, price sign, marked vehicle lanes, curb islands, bollards, bins, and calibrated canopy lighting.",
    [
      "convenience building",
      "supported canopy",
      "four pumps",
      "price sign",
      "vehicle lanes",
      "bollards",
    ],
    ["perspective", "front", "top"],
    {
      max_duplicate_count: 4,
    },
  ),
  create_benchmark(
    "gas_station_abandoned",
    "gas_station",
    "Abandoned rural station",
    "Create an abandoned rural gas station with a weathered service building, damaged canopy, two old pumps, broken sign, weeds, scattered debris, cracked pavement, and overcast lighting. Preserve structural credibility despite the decay.",
    [
      "service building",
      "damaged canopy",
      "old pumps",
      "broken sign",
      "weeds",
      "cracked pavement",
    ],
    ["perspective", "front", "detail"],
  ),
  create_benchmark(
    "airport_departure_gate",
    "airport",
    "Airport departure gate",
    "Create a modern airport departure gate with a glazed exterior wall, gate desk, boarding lane barriers, seating groups, flight information displays, ceiling services, floor pattern, and daylight balanced with interior lighting.",
    [
      "glazed wall",
      "gate desk",
      "boarding lanes",
      "seating groups",
      "flight displays",
      "ceiling services",
    ],
    ["perspective", "front", "top"],
    {
      max_duplicate_count: 8,
    },
  ),
  create_benchmark(
    "airport_baggage_claim",
    "airport",
    "Airport baggage claim",
    "Create an airport baggage claim hall with a complete carousel, support columns, overhead wayfinding, luggage carts, seating, service doors, ceiling lights, and differentiated floor and wall materials. Maintain clear circulation around the carousel.",
    [
      "baggage carousel",
      "support columns",
      "wayfinding",
      "luggage carts",
      "service doors",
      "circulation",
    ],
    ["perspective", "side", "top"],
    {
      max_duplicate_count: 6,
    },
  ),
  create_benchmark(
    "warehouse_loading_bay",
    "warehouse",
    "Warehouse loading bay",
    "Create a working warehouse loading bay with a structural frame, dock doors, dock levelers, protective bollards, loading markings, pallet stacks, utility fixtures, and exterior floodlights. Show believable support and vehicle access.",
    [
      "structural frame",
      "dock doors",
      "dock levelers",
      "bollards",
      "pallet stacks",
      "floodlights",
    ],
    ["perspective", "front", "side"],
    {
      max_duplicate_count: 6,
    },
  ),
  create_benchmark(
    "warehouse_cold_storage",
    "warehouse",
    "Cold storage interior",
    "Create a cold storage warehouse interior with insulated wall panels, tall rack aisles, varied pallet loads, evaporator units, safety barriers, floor markings, pipes, and cool industrial lighting. Keep all aisles navigable.",
    [
      "insulated panels",
      "rack aisles",
      "pallet loads",
      "evaporator units",
      "safety barriers",
      "pipes",
    ],
    ["perspective", "top", "detail"],
    {
      max_duplicate_count: 10,
    },
  ),
  create_benchmark(
    "road_mountain_checkpoint",
    "road",
    "Mountain road checkpoint",
    "Create a mountain road checkpoint with a curved two-lane road, shoulder, guardrails, inspection booth, barrier arms, warning signs, traffic cones, drainage, rock embankments, and clear approach lighting.",
    [
      "curved road",
      "shoulder",
      "guardrails",
      "inspection booth",
      "barrier arms",
      "drainage",
    ],
    ["perspective", "top", "side"],
  ),
  create_benchmark(
    "road_wet_urban_night",
    "road",
    "Wet urban intersection",
    "Create a wet urban intersection at night with marked lanes, crosswalks, traffic lights on supported poles, sidewalks, curb ramps, storm drains, street furniture, reflective signs, and pools of warm and cool light.",
    [
      "marked lanes",
      "crosswalks",
      "traffic lights",
      "curb ramps",
      "storm drains",
      "reflective signs",
    ],
    ["perspective", "top", "detail"],
    {
      max_default_material_ratio: 0.1,
    },
  ),
  create_benchmark(
    "prop_vintage_camera",
    "prop",
    "Vintage film camera",
    "Create a detailed vintage film camera prop with a stepped body, lens assembly, focus and aperture rings, viewfinder, winding controls, strap mounts, engraved panel shapes, and distinct leather, metal, and glass materials.",
    [
      "stepped body",
      "lens assembly",
      "control rings",
      "viewfinder",
      "winding controls",
      "strap mounts",
    ],
    ["perspective", "front", "side", "detail"],
    {
      max_duplicate_count: 1,
      max_default_material_ratio: 0.05,
    },
  ),
  create_benchmark(
    "prop_industrial_generator",
    "prop",
    "Portable generator",
    "Create a detailed portable industrial generator with a tubular protective frame, engine housing, fuel tank, control panel, sockets, wheels, feet, vents, handles, fasteners, and coordinated painted metal, rubber, and warning materials.",
    [
      "protective frame",
      "engine housing",
      "fuel tank",
      "control panel",
      "wheels",
      "vents",
    ],
    ["perspective", "front", "side", "detail"],
    {
      max_duplicate_count: 2,
      max_default_material_ratio: 0.05,
    },
  ),
  create_benchmark(
    "lighting_gallery_variants",
    "lighting_material",
    "Gallery lighting variants",
    "Create a small gallery containing the same sculpture display under three clearly separated lighting setups: soft daylight, warm spotlights, and cool dramatic rim lighting. Keep geometry consistent while making each lighting intent readable.",
    [
      "three display zones",
      "consistent sculpture",
      "soft daylight",
      "warm spotlights",
      "cool rim lighting",
      "zone separation",
    ],
    ["perspective", "front", "top"],
    {
      max_duplicate_count: 3,
    },
  ),
  create_benchmark(
    "material_kiosk_variants",
    "lighting_material",
    "Kiosk material variants",
    "Create three matching retail kiosks that share geometry but use distinct material palettes: natural wood and brass, painted steel and glass, and recycled plastic with rubber trim. Each palette must preserve readable material separation.",
    [
      "three matching kiosks",
      "wood and brass",
      "steel and glass",
      "recycled plastic",
      "rubber trim",
      "material separation",
    ],
    ["perspective", "front", "detail"],
    {
      max_duplicate_count: 3,
      max_default_material_ratio: 0.05,
    },
  ),
];

function deep_freeze(value) {
  if (
    !value ||
    typeof value !== "object" ||
    Object.isFrozen(value)
  )
  {
    return value;
  }
  Object.freeze(value);
  for (const child of Object.values(value))
  {
    deep_freeze(child);
  }
  return value;
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function clamp(value, minimum, maximum) {
  return Math.max(
    minimum,
    Math.min(maximum, value),
  );
}

function round(value, digits = 2) {
  const scale = 10 ** digits;
  return Math.round(value * scale) / scale;
}

function add_error(errors, path, code, message) {
  errors.push({
    path,
    code,
    message,
  });
}

function validate_unit_number(errors, result, name) {
  const value = result[name];
  if (!Number.isFinite(value))
  {
    add_error(
      errors,
      name,
      "invalid_number",
      `${name} must be a finite number`,
    );
    return;
  }
  if (value < 0 || value > 1)
  {
    add_error(
      errors,
      name,
      "out_of_range",
      `${name} must be between 0 and 1`,
    );
  }
}

function validate_count(errors, result, name) {
  const value = result[name];
  if (
    !Number.isInteger(value) ||
    value < 0
  )
  {
    add_error(
      errors,
      name,
      "invalid_count",
      `${name} must be a non-negative integer`,
    );
  }
}

function benchmark_by_id(benchmark_id) {
  return built_in_benchmarks.find(
    (benchmark) => benchmark.id === benchmark_id,
  );
}

function result_payload(value) {
  if (
    value &&
    typeof value === "object" &&
    value.result &&
    typeof value.result === "object"
  )
  {
    return value.result;
  }
  return value;
}

deep_freeze(camera_views);
deep_freeze(human_rubric);
deep_freeze(score_weights);
deep_freeze(built_in_benchmarks);

export const scene_benchmark_camera_views = camera_views;
export const scene_benchmark_human_rubric = human_rubric;
export const scene_benchmark_score_weights = score_weights;
export const scene_benchmarks = built_in_benchmarks;

export function list_benchmarks(
  {
    category,
    include_prompts = true,
  } = {},
) {
  const categories = [
    ...new Set(
      built_in_benchmarks.map(
        (benchmark) => benchmark.category,
      ),
    ),
  ];
  if (
    category !== undefined &&
    !categories.includes(category)
  )
  {
    return {
      ok: false,
      error: `unknown benchmark category ${category}`,
      code: "benchmark_category_not_found",
      available_categories: categories,
    };
  }

  const matching = built_in_benchmarks.filter(
    (benchmark) =>
      category === undefined ||
      benchmark.category === category,
  );
  const benchmarks = matching.map((benchmark) => {
    if (include_prompts)
    {
      return clone(benchmark);
    }
    return {
      id: benchmark.id,
      category: benchmark.category,
      title: benchmark.title,
      camera_view_ids: [...benchmark.camera_view_ids],
      pass_score: benchmark.scoring.pass_score,
    };
  });

  return {
    ok: true,
    count: benchmarks.length,
    categories,
    benchmarks,
  };
}

export function get_benchmark(benchmark_id) {
  const benchmark = benchmark_by_id(benchmark_id);
  if (!benchmark)
  {
    return {
      ok: false,
      error: `unknown benchmark ${benchmark_id}`,
      code: "benchmark_not_found",
      available_ids: built_in_benchmarks.map(
        (entry) => entry.id,
      ),
    };
  }

  return {
    ok: true,
    benchmark: clone(benchmark),
    camera_views: benchmark.camera_view_ids.map(
      (view_id) => clone(camera_views[view_id]),
    ),
    human_rubric: clone(human_rubric),
    score_weights: clone(score_weights),
    visual_metric_names: [...visual_metric_names],
  };
}

export function validate_benchmark_result(input) {
  const result = result_payload(input);
  const errors = [];
  if (
    !result ||
    typeof result !== "object" ||
    Array.isArray(result)
  )
  {
    add_error(
      errors,
      "result",
      "invalid_type",
      "benchmark result must be an object",
    );
    return {
      ok: true,
      valid: false,
      error_count: errors.length,
      errors,
    };
  }

  const benchmark = benchmark_by_id(result.benchmark_id);
  if (!benchmark)
  {
    add_error(
      errors,
      "benchmark_id",
      "benchmark_not_found",
      `unknown benchmark ${result.benchmark_id}`,
    );
  }

  validate_unit_number(
    errors,
    result,
    "plan_fidelity",
  );
  validate_count(
    errors,
    result,
    "duplicate_count",
  );
  validate_unit_number(
    errors,
    result,
    "default_material_ratio",
  );
  if (typeof result.audit_pass !== "boolean")
  {
    add_error(
      errors,
      "audit_pass",
      "invalid_boolean",
      "audit_pass must be a boolean",
    );
  }
  validate_count(
    errors,
    result,
    "correction_count",
  );

  if (
    result.human_scores !== undefined &&
    (
      !result.human_scores ||
      typeof result.human_scores !== "object" ||
      Array.isArray(result.human_scores)
    )
  )
  {
    add_error(
      errors,
      "human_scores",
      "invalid_type",
      "human_scores must be an object",
    );
  }
  else if (result.human_scores)
  {
    for (const name of Object.keys(human_rubric))
    {
      const score = result.human_scores[name];
      if (
        !Number.isFinite(score) ||
        score < 1 ||
        score > 5
      )
      {
        add_error(
          errors,
          `human_scores.${name}`,
          "out_of_range",
          `${name} must be between 1 and 5`,
        );
      }
    }
    for (const name of Object.keys(result.human_scores))
    {
      if (!human_rubric[name])
      {
        add_error(
          errors,
          `human_scores.${name}`,
          "unknown_dimension",
          `unknown human score dimension ${name}`,
        );
      }
    }
  }

  if (result.visual_metrics !== undefined)
  {
    if (
      !result.visual_metrics ||
      typeof result.visual_metrics !== "object" ||
      Array.isArray(result.visual_metrics)
    )
    {
      add_error(
        errors,
        "visual_metrics",
        "invalid_type",
        "visual_metrics must be an object",
      );
    }
    else
    {
      const names = Object.keys(result.visual_metrics);
      if (names.length === 0)
      {
        add_error(
          errors,
          "visual_metrics",
          "empty_metrics",
          "visual_metrics must include at least one metric",
        );
      }
      for (const name of names)
      {
        if (!visual_metric_names.includes(name))
        {
          add_error(
            errors,
            `visual_metrics.${name}`,
            "unknown_metric",
            `unknown visual metric ${name}`,
          );
          continue;
        }
        const value = result.visual_metrics[name];
        if (
          !Number.isFinite(value) ||
          value < 0 ||
          value > 1
        )
        {
          add_error(
            errors,
            `visual_metrics.${name}`,
            "out_of_range",
            `${name} must be between 0 and 1`,
          );
        }
      }
    }
  }

  const valid = errors.length === 0;
  return {
    ok: true,
    valid,
    error_count: errors.length,
    errors,
    benchmark_id: result.benchmark_id,
    normalized_result: valid
      ? clone(result)
      : undefined,
  };
}

export function calculate_benchmark_metrics(input) {
  const validation = validate_benchmark_result(input);
  if (!validation.valid)
  {
    return {
      ok: false,
      error: "invalid benchmark result",
      code: "invalid_benchmark_result",
      validation,
    };
  }

  const result = validation.normalized_result;
  const benchmark = benchmark_by_id(
    result.benchmark_id,
  );
  const duplicate_excess = Math.max(
    0,
    result.duplicate_count -
      benchmark.scoring.max_duplicate_count,
  );
  const duplicate_control = clamp(
    1 - duplicate_excess / 5,
    0,
    1,
  );
  const material_coverage = clamp(
    1 - result.default_material_ratio,
    0,
    1,
  );
  let correction_efficiency = 1;
  if (
    result.correction_count <
    benchmark.scoring.preferred_correction_count
  )
  {
    correction_efficiency = 0.5;
  }
  else if (
    result.correction_count >
    benchmark.scoring.max_correction_count
  )
  {
    const excess =
      result.correction_count -
      benchmark.scoring.max_correction_count;
    correction_efficiency = clamp(
      1 - excess / 5,
      0,
      1,
    );
  }

  const human_values = result.human_scores
    ? Object.keys(human_rubric).map(
      (name) => result.human_scores[name],
    )
    : [];
  const human_average = human_values.length > 0
    ? human_values.reduce(
      (sum, value) => sum + value,
      0,
    ) / human_values.length
    : undefined;
  const human_score = human_average === undefined
    ? undefined
    : (human_average - 1) / 4;
  const visual_values = result.visual_metrics
    ? Object.values(result.visual_metrics)
    : [];
  const visual_score = visual_values.length > 0
    ? visual_values.reduce(
        (sum, value) => sum + value,
        0,
      ) / visual_values.length
    : undefined;
  const normalized_metrics = {
    plan_fidelity: result.plan_fidelity,
    duplicate_control,
    material_coverage,
    audit_pass: result.audit_pass ? 1 : 0,
    correction_efficiency,
  };
  if (human_score !== undefined)
  {
    normalized_metrics.human_score = human_score;
  }
  if (visual_score !== undefined)
  {
    normalized_metrics.visual_score = visual_score;
  }

  const active_weight_total = Object.entries(
    score_weights,
  ).reduce(
    (sum, [name, weight]) =>
      normalized_metrics[name] === undefined
        ? sum
        : sum + weight,
    0,
  );
  const applied_weights = {};
  const weighted_contributions = {};
  let weighted_total = 0;
  for (
    const [name, value] of Object.entries(
      normalized_metrics,
    )
  )
  {
    const applied_weight =
      score_weights[name] / active_weight_total;
    const contribution =
      value * applied_weight * 100;
    applied_weights[name] = round(
      applied_weight,
      4,
    );
    weighted_contributions[name] = round(
      contribution,
      2,
    );
    weighted_total += contribution;
  }

  const weighted_score = round(weighted_total, 2);
  const threshold_checks = {
    score: weighted_score >=
      benchmark.scoring.pass_score,
    audit: result.audit_pass,
    default_material_ratio:
      result.default_material_ratio <=
      benchmark.scoring.max_default_material_ratio,
    duplicate_count:
      result.duplicate_count <=
      benchmark.scoring.max_duplicate_count,
    human_acceptance:
      human_average !== undefined &&
      human_average >=
        benchmark.scoring.min_human_average,
    visual_acceptance:
      visual_score !== undefined &&
      visual_score >=
        benchmark.scoring.min_visual_score,
  };
  const failed_checks = Object.entries(
    threshold_checks,
  )
    .filter(([, pass]) => !pass)
    .map(([name]) => name);
  const automated_failed_checks = failed_checks.filter(
    (name) =>
      name !== "human_acceptance" &&
      name !== "visual_acceptance",
  );

  return {
    ok: true,
    benchmark_id: result.benchmark_id,
    benchmark_title: benchmark.title,
    weighted_score,
    pass: failed_checks.length === 0,
    automated_pass:
      automated_failed_checks.length === 0,
    acceptance_status: failed_checks.length === 0
      ? "accepted"
      : (
          human_average === undefined ||
          visual_score === undefined
        )
        ? "rating_pending"
        : "rejected",
    pass_score: benchmark.scoring.pass_score,
    min_human_average:
      benchmark.scoring.min_human_average,
    min_visual_score:
      benchmark.scoring.min_visual_score,
    metrics: {
      plan_fidelity: round(
        result.plan_fidelity * 100,
        2,
      ),
      duplicate_count: result.duplicate_count,
      duplicate_control: round(
        duplicate_control * 100,
        2,
      ),
      default_material_ratio: round(
        result.default_material_ratio,
        4,
      ),
      material_coverage: round(
        material_coverage * 100,
        2,
      ),
      audit_pass: result.audit_pass,
      correction_count: result.correction_count,
      correction_efficiency: round(
        correction_efficiency * 100,
        2,
      ),
      human_average: human_average === undefined
        ? null
        : round(human_average, 2),
      human_score: human_score === undefined
        ? null
        : round(human_score * 100, 2),
      visual_score: visual_score === undefined
        ? null
        : round(visual_score * 100, 2),
    },
    applied_weights,
    weighted_contributions,
    threshold_checks,
    failed_checks,
    automated_failed_checks,
    result,
  };
}

export function compare_benchmark_results(
  current_input,
  baseline_input,
) {
  const current = calculate_benchmark_metrics(
    current_input,
  );
  if (!current.ok)
  {
    return {
      ok: false,
      error: "current benchmark result is invalid",
      code: "invalid_current_result",
      current,
    };
  }

  const baseline = calculate_benchmark_metrics(
    baseline_input,
  );
  if (!baseline.ok)
  {
    return {
      ok: false,
      error: "baseline benchmark result is invalid",
      code: "invalid_baseline_result",
      baseline,
    };
  }
  if (current.benchmark_id !== baseline.benchmark_id)
  {
    return {
      ok: false,
      error: "current and baseline benchmark ids differ",
      code: "benchmark_mismatch",
      current_benchmark_id: current.benchmark_id,
      baseline_benchmark_id: baseline.benchmark_id,
    };
  }

  const score_delta = round(
    current.weighted_score -
      baseline.weighted_score,
    2,
  );
  const metric_deltas = {};
  for (const name of [
    "plan_fidelity",
    "duplicate_count",
    "default_material_ratio",
    "correction_count",
    "human_average",
    "human_score",
    "visual_score",
  ])
  {
    const current_value = current.metrics[name];
    const baseline_value = baseline.metrics[name];
    metric_deltas[name] =
      current_value === null ||
      baseline_value === null
        ? null
        : round(
            current_value - baseline_value,
            4,
          );
  }

  return {
    ok: true,
    benchmark_id: current.benchmark_id,
    comparison: score_delta > 0
      ? "improved"
      : score_delta < 0
        ? "regressed"
        : "unchanged",
    score_delta,
    pass_changed: current.pass !== baseline.pass,
    current: {
      weighted_score: current.weighted_score,
      pass: current.pass,
      metrics: current.metrics,
    },
    baseline: {
      weighted_score: baseline.weighted_score,
      pass: baseline.pass,
      metrics: baseline.metrics,
    },
    metric_deltas,
  };
}
