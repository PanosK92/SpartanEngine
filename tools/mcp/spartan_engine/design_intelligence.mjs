const scene_roles = Object.freeze([
  "surface",
  "structure",
  "route",
  "functional",
  "furnishing",
  "prop",
  "detail",
  "light",
]);

const support_modes = Object.freeze([
  "ground",
  "surface",
  "wall",
  "ceiling",
  "suspended",
  "none",
]);

const relationship_types = Object.freeze([
  "on",
  "inside",
  "connected_to",
  "separated_from",
  "aligned_with",
  "beside",
]);

export const semantic_role_catalog = Object.freeze({
  entrance: Object.freeze({
    role: "functional",
    tags: Object.freeze([
      "entrance",
      "threshold",
      "access",
    ]),
    intent: "primary point of arrival",
  }),
  support: Object.freeze({
    role: "structure",
    tags: Object.freeze([
      "support",
      "load_bearing",
      "structural",
    ]),
    intent: "visible structural support",
  }),
  walkable: Object.freeze({
    role: "surface",
    tags: Object.freeze([
      "walkable",
      "circulation",
      "grounded",
    ]),
    intent: "surface intended for movement",
  }),
  focal_point: Object.freeze({
    role: "functional",
    tags: Object.freeze([
      "focal_point",
      "landmark",
      "visual_anchor",
    ]),
    intent: "primary visual destination",
  }),
  service_route: Object.freeze({
    role: "route",
    tags: Object.freeze([
      "service_route",
      "back_of_house",
      "operations",
    ]),
    intent: "operational movement path",
  }),
});

export const semantic_tag_catalog = Object.freeze(
  Object.fromEntries(
    Object.entries(semantic_role_catalog)
      .flatMap(([semantic_role, entry]) =>
        entry.tags.map((tag) => [
          tag,
          Object.freeze({
            semantic_role,
            scene_role: entry.role,
          }),
        ]),
      ),
  ),
);

const palette_rules = Object.freeze({
  value_hierarchy: Object.freeze({
    dominant: Object.freeze([0.18, 0.45]),
    secondary: Object.freeze([0.35, 0.65]),
    focal: Object.freeze([0.62, 0.9]),
  }),
  accent_ratio: Object.freeze([0.05, 0.12]),
  roughness_ranges: Object.freeze({
    painted_metal: Object.freeze([0.28, 0.52]),
    bare_metal: Object.freeze([0.16, 0.36]),
    masonry: Object.freeze([0.62, 0.9]),
    glass: Object.freeze([0.04, 0.18]),
    wood: Object.freeze([0.38, 0.72]),
    asphalt: Object.freeze([0.72, 0.94]),
    plastic: Object.freeze([0.3, 0.58]),
  }),
  texture_scale: Object.freeze({
    rule: "use real world meters",
    macro_variation_m: Object.freeze([2, 8]),
    surface_pattern_m: Object.freeze([0.08, 1.2]),
    micro_detail_m: Object.freeze([0.001, 0.04]),
  }),
  wear: Object.freeze({
    coverage: Object.freeze([0.03, 0.18]),
    placement: Object.freeze([
      "contact_edges",
      "traffic_paths",
      "weather_exposure",
      "service_points",
    ]),
    rule: "wear follows use and exposure",
  }),
});

function zone(name, purpose, center, size)
{
  return {
    name,
    purpose,
    center,
    size,
  };
}

function element(
  name,
  role,
  zone_name,
  expected_size,
  support,
  material_semantic,
)
{
  return {
    name,
    role,
    zone: zone_name,
    expected_size,
    support,
    material_semantic,
  };
}

function relationship(subject, relation, object)
{
  return {
    subject,
    relation,
    object,
  };
}

const templates = {
  room: {
    keywords: [
      "room",
      "interior",
      "bedroom",
      "office",
      "lounge",
      "kitchen",
    ],
    use_case: "human scale interior",
    style: "cohesive architectural interior",
    era: "contemporary",
    mood: "welcoming and legible",
    scale_references: [
      {
        name: "interior_door",
        size: [0.9, 2.1, 0.12],
        rationale: "a standard door anchors human scale",
      },
    ],
    focal_hierarchy: [
      "primary_feature",
      "seating_group",
      "entrance",
    ],
    palette: {
      dominant: ["warm_neutral", "muted_wall"],
      secondary: ["natural_wood", "soft_textile"],
      accent: ["restrained_warm_accent"],
    },
    material_vocabulary: [
      "painted_plaster",
      "natural_wood",
      "woven_textile",
      "clear_glass",
      "brushed_metal",
    ],
    lighting_scenario: {
      intent: "soft daylight with warm practical pools",
      min_lights: 2,
      max_lights: 6,
      require_shadows: true,
    },
    detail_budget: {
      hero: 1,
      supporting: 4,
      repeated: 8,
      micro_detail_density: "medium",
    },
    metric_rules: {
      footprint_m: [[3, 12], [3, 12]],
      ceiling_height_m: [2.4, 4.5],
      circulation_width_m: [0.8, 1.5],
    },
    zones: [
      zone(
        "interior",
        "occupied room volume",
        [0, 1.5, 0],
        [6, 3, 5],
      ),
      zone(
        "circulation",
        "clear entrance path",
        [0, 0.05, 1.5],
        [1.2, 0.1, 2],
      ),
    ],
    elements: [
      element(
        "floor",
        "surface",
        "interior",
        [6, 0.12, 5],
        "ground",
        "wood",
      ),
      element(
        "room_shell",
        "structure",
        "interior",
        [6, 3, 5],
        "ground",
        "painted_plaster",
      ),
      element(
        "entrance_door",
        "functional",
        "interior",
        [0.9, 2.1, 0.12],
        "wall",
        "wood",
      ),
      element(
        "primary_feature",
        "furnishing",
        "interior",
        [2.2, 0.9, 1],
        "surface",
        "textile",
      ),
      element(
        "circulation_path",
        "route",
        "circulation",
        [1.2, 0.05, 2],
        "surface",
        "floor_finish",
      ),
      element(
        "ceiling_light",
        "light",
        "interior",
        [0.5, 0.15, 0.5],
        "ceiling",
        "emissive_fixture",
      ),
    ],
    relationships: [
      relationship(
        "primary_feature",
        "on",
        "floor",
      ),
      relationship(
        "circulation_path",
        "connected_to",
        "entrance_door",
      ),
    ],
  },
  storefront: {
    keywords: [
      "storefront",
      "shop",
      "retail",
      "boutique",
      "cafe",
      "restaurant",
    ],
    use_case: "street facing retail facade",
    style: "clear branded storefront",
    era: "contemporary",
    mood: "inviting and active",
    scale_references: [
      {
        name: "double_entry_door",
        size: [1.8, 2.2, 0.12],
        rationale: "the entry establishes pedestrian scale",
      },
    ],
    focal_hierarchy: [
      "brand_sign",
      "display_window",
      "entry",
    ],
    palette: {
      dominant: ["charcoal_frame", "warm_interior"],
      secondary: ["clear_glass", "masonry"],
      accent: ["brand_color"],
    },
    material_vocabulary: [
      "powder_coated_metal",
      "clear_glass",
      "painted_masonry",
      "natural_wood",
      "emissive_signage",
    ],
    lighting_scenario: {
      intent: "warm interior glow with a readable sign",
      min_lights: 3,
      max_lights: 8,
      require_shadows: true,
    },
    detail_budget: {
      hero: 2,
      supporting: 5,
      repeated: 10,
      micro_detail_density: "medium",
    },
    metric_rules: {
      facade_width_m: [5, 18],
      entry_width_m: [1, 2.4],
      glazing_ratio: [0.45, 0.75],
    },
    zones: [
      zone(
        "facade",
        "public facing elevation",
        [0, 2.5, 0],
        [10, 5, 1],
      ),
      zone(
        "threshold",
        "pedestrian arrival",
        [0, 0.05, 1.2],
        [4, 0.1, 2],
      ),
    ],
    elements: [
      element(
        "facade_shell",
        "structure",
        "facade",
        [10, 5, 1],
        "ground",
        "masonry",
      ),
      element(
        "entry",
        "functional",
        "facade",
        [1.8, 2.2, 0.12],
        "ground",
        "glass_and_metal",
      ),
      element(
        "display_window",
        "functional",
        "facade",
        [4, 2.6, 0.12],
        "wall",
        "clear_glass",
      ),
      element(
        "brand_sign",
        "detail",
        "facade",
        [4, 0.8, 0.15],
        "wall",
        "emissive_signage",
      ),
      element(
        "entry_path",
        "route",
        "threshold",
        [2, 0.1, 2],
        "ground",
        "paving",
      ),
      element(
        "display_lights",
        "light",
        "facade",
        [0.2, 0.2, 0.2],
        "ceiling",
        "metal_fixture",
      ),
      {
        ...element(
          "sign_light",
          "light",
          "facade",
          [0.3, 0.3, 0.3],
          "wall",
          "metal_fixture",
        ),
        count: 2,
      },
    ],
    relationships: [
      relationship("entry_path", "connected_to", "entry"),
      relationship("brand_sign", "aligned_with", "entry"),
      relationship("display_window", "beside", "entry"),
    ],
  },
  gas_station: {
    keywords: [
      "gas station",
      "gas_station",
      "petrol station",
      "fuel station",
      "service station",
      "fuel pumps",
    ],
    use_case: "vehicle fueling and convenience retail",
    style: "high visibility roadside facility",
    era: "contemporary",
    mood: "safe and operational",
    scale_references: [
      {
        name: "passenger_vehicle",
        size: [1.9, 1.5, 4.6],
        rationale: "vehicle size controls pump spacing",
      },
    ],
    focal_hierarchy: [
      "canopy",
      "price_sign",
      "storefront",
    ],
    palette: {
      dominant: ["light_canopy", "dark_asphalt"],
      secondary: ["concrete", "storefront_glass"],
      accent: ["brand_color"],
    },
    material_vocabulary: [
      "painted_metal",
      "concrete",
      "asphalt",
      "clear_glass",
      "safety_plastic",
      "rubber",
    ],
    lighting_scenario: {
      intent: "bright canopy pools with safe route coverage",
      min_lights: 6,
      max_lights: 16,
      require_shadows: true,
    },
    detail_budget: {
      hero: 2,
      supporting: 8,
      repeated: 16,
      micro_detail_density: "medium",
    },
    metric_rules: {
      pump_lane_width_m: [3.5, 4.5],
      canopy_clearance_m: [4.2, 5.5],
      turning_radius_m: [7, 12],
    },
    zones: [
      zone(
        "forecourt",
        "vehicle fueling",
        [0, 0.05, 0],
        [28, 0.1, 22],
      ),
      zone(
        "retail",
        "convenience store",
        [0, 2.2, -13],
        [14, 4.4, 7],
      ),
    ],
    elements: [
      element(
        "forecourt_surface",
        "surface",
        "forecourt",
        [28, 0.2, 22],
        "ground",
        "concrete",
      ),
      element(
        "canopy",
        "structure",
        "forecourt",
        [18, 1, 10],
        "ground",
        "painted_metal",
      ),
      element(
        "fuel_pumps",
        "functional",
        "forecourt",
        [1, 2.2, 0.8],
        "ground",
        "painted_metal",
      ),
      element(
        "vehicle_route",
        "route",
        "forecourt",
        [24, 0.05, 8],
        "surface",
        "lane_marking",
      ),
      element(
        "storefront",
        "structure",
        "retail",
        [14, 4.4, 7],
        "ground",
        "masonry_and_glass",
      ),
      element(
        "price_sign",
        "detail",
        "forecourt",
        [2.5, 7, 0.5],
        "ground",
        "emissive_signage",
      ),
      {
        ...element(
          "canopy_lights",
          "light",
          "forecourt",
          [0.8, 0.15, 0.8],
          "ceiling",
          "emissive_fixture",
        ),
        count: 6,
      },
    ],
    relationships: [
      relationship("fuel_pumps", "on", "forecourt_surface"),
      relationship("vehicle_route", "beside", "fuel_pumps"),
      relationship("vehicle_route", "connected_to", "storefront"),
    ],
  },
  airport: {
    keywords: [
      "airport",
      "terminal",
      "airfield",
      "runway",
      "hangar",
      "concourse",
    ],
    use_case: "passenger aviation terminal",
    style: "large span civic infrastructure",
    era: "contemporary",
    mood: "ordered and expansive",
    scale_references: [
      {
        name: "narrow_body_aircraft",
        size: [35.8, 12.5, 39.5],
        rationale: "aircraft size anchors apron clearance",
      },
    ],
    focal_hierarchy: [
      "terminal_hall",
      "control_tower",
      "gate_bridge",
    ],
    palette: {
      dominant: ["cool_structure", "neutral_concrete"],
      secondary: ["clear_glass", "dark_tarmac"],
      accent: ["wayfinding_color"],
    },
    material_vocabulary: [
      "structural_steel",
      "architectural_glass",
      "polished_concrete",
      "tarmac",
      "painted_aluminum",
    ],
    lighting_scenario: {
      intent: "broad terminal illumination and apron safety",
      min_lights: 12,
      max_lights: 48,
      require_shadows: true,
    },
    detail_budget: {
      hero: 3,
      supporting: 12,
      repeated: 32,
      micro_detail_density: "low_at_distance",
    },
    metric_rules: {
      gate_spacing_m: [45, 75],
      terminal_height_m: [12, 35],
      service_lane_width_m: [4, 7],
    },
    zones: [
      zone(
        "terminal",
        "passenger processing",
        [0, 12, -40],
        [120, 24, 45],
      ),
      zone(
        "apron",
        "aircraft stand and servicing",
        [0, 0.1, 35],
        [150, 0.2, 100],
      ),
    ],
    elements: [
      element(
        "terminal_hall",
        "structure",
        "terminal",
        [120, 24, 45],
        "ground",
        "steel_and_glass",
      ),
      element(
        "apron_surface",
        "surface",
        "apron",
        [150, 0.3, 100],
        "ground",
        "concrete",
      ),
      element(
        "gate_bridge",
        "functional",
        "apron",
        [22, 4, 4],
        "ground",
        "painted_metal",
      ),
      element(
        "service_route",
        "route",
        "apron",
        [100, 0.05, 6],
        "surface",
        "lane_marking",
      ),
      element(
        "control_tower",
        "structure",
        "terminal",
        [12, 42, 12],
        "ground",
        "concrete_and_glass",
      ),
      {
        ...element(
          "apron_lights",
          "light",
          "apron",
          [1, 18, 1],
          "ground",
          "painted_metal",
        ),
        count: 12,
      },
    ],
    relationships: [
      relationship("gate_bridge", "connected_to", "terminal_hall"),
      relationship("service_route", "beside", "gate_bridge"),
      relationship("control_tower", "separated_from", "apron_lights"),
    ],
  },
  road: {
    keywords: [
      "road",
      "street",
      "highway",
      "motorway",
      "intersection",
      "bridge",
    ],
    use_case: "vehicle and pedestrian circulation",
    style: "legible transport infrastructure",
    era: "contemporary",
    mood: "directional and grounded",
    scale_references: [
      {
        name: "traffic_lane",
        size: [3.5, 0.05, 20],
        rationale: "lane width anchors roadway scale",
      },
    ],
    focal_hierarchy: [
      "road_alignment",
      "intersection",
      "signage",
    ],
    palette: {
      dominant: ["dark_asphalt", "concrete"],
      secondary: ["roadside_green", "galvanized_metal"],
      accent: ["safety_marking"],
    },
    material_vocabulary: [
      "asphalt",
      "cast_concrete",
      "galvanized_steel",
      "road_paint",
      "weathered_stone",
    ],
    lighting_scenario: {
      intent: "even route visibility with readable junctions",
      min_lights: 6,
      max_lights: 24,
      require_shadows: true,
    },
    detail_budget: {
      hero: 1,
      supporting: 6,
      repeated: 24,
      micro_detail_density: "low_at_speed",
    },
    metric_rules: {
      lane_width_m: [3, 3.75],
      sidewalk_width_m: [1.5, 3],
      curb_height_m: [0.1, 0.18],
    },
    zones: [
      zone(
        "carriageway",
        "vehicle movement",
        [0, 0.1, 0],
        [14, 0.2, 80],
      ),
      zone(
        "roadside",
        "pedestrian and service edge",
        [9, 0.1, 0],
        [4, 0.2, 80],
      ),
    ],
    elements: [
      element(
        "road_surface",
        "surface",
        "carriageway",
        [14, 0.2, 80],
        "ground",
        "asphalt",
      ),
      element(
        "lane_route",
        "route",
        "carriageway",
        [3.5, 0.03, 80],
        "surface",
        "road_paint",
      ),
      element(
        "sidewalk",
        "surface",
        "roadside",
        [2, 0.18, 80],
        "ground",
        "concrete",
      ),
      element(
        "guardrail",
        "structure",
        "roadside",
        [0.3, 0.8, 40],
        "ground",
        "galvanized_steel",
      ),
      element(
        "direction_sign",
        "detail",
        "roadside",
        [2.4, 1.5, 0.12],
        "ground",
        "painted_metal",
      ),
      {
        ...element(
          "street_lights",
          "light",
          "roadside",
          [0.3, 8, 0.3],
          "ground",
          "painted_metal",
        ),
        count: 6,
      },
    ],
    relationships: [
      relationship("lane_route", "on", "road_surface"),
      relationship("sidewalk", "beside", "road_surface"),
      relationship("guardrail", "aligned_with", "road_surface"),
    ],
  },
  warehouse: {
    keywords: [
      "warehouse",
      "distribution center",
      "factory",
      "industrial",
      "loading dock",
      "logistics",
    ],
    use_case: "storage and logistics operations",
    style: "functional industrial architecture",
    era: "contemporary",
    mood: "robust and efficient",
    scale_references: [
      {
        name: "loading_bay",
        size: [3.5, 4.5, 3],
        rationale: "a loading bay anchors truck operations",
      },
    ],
    focal_hierarchy: [
      "loading_bays",
      "storage_racks",
      "office_entry",
    ],
    palette: {
      dominant: ["neutral_cladding", "concrete"],
      secondary: ["structural_steel", "safety_gray"],
      accent: ["safety_yellow"],
    },
    material_vocabulary: [
      "corrugated_metal",
      "cast_concrete",
      "painted_steel",
      "industrial_glass",
      "rubber",
    ],
    lighting_scenario: {
      intent: "uniform high bay lighting with loading emphasis",
      min_lights: 8,
      max_lights: 32,
      require_shadows: true,
    },
    detail_budget: {
      hero: 1,
      supporting: 10,
      repeated: 24,
      micro_detail_density: "medium_at_work_zones",
    },
    metric_rules: {
      clear_height_m: [8, 15],
      aisle_width_m: [3.2, 4.5],
      loading_bay_width_m: [3.2, 4],
    },
    zones: [
      zone(
        "storage",
        "racked goods storage",
        [0, 6, 0],
        [50, 12, 40],
      ),
      zone(
        "loading",
        "truck loading and service",
        [0, 2, 25],
        [40, 4, 10],
      ),
    ],
    elements: [
      element(
        "warehouse_shell",
        "structure",
        "storage",
        [50, 12, 40],
        "ground",
        "corrugated_metal",
      ),
      element(
        "floor_slab",
        "surface",
        "storage",
        [50, 0.3, 40],
        "ground",
        "concrete",
      ),
      element(
        "storage_racks",
        "functional",
        "storage",
        [2, 8, 18],
        "surface",
        "painted_steel",
      ),
      element(
        "service_route",
        "route",
        "storage",
        [4, 0.05, 32],
        "surface",
        "safety_marking",
      ),
      element(
        "loading_bays",
        "functional",
        "loading",
        [3.5, 4.5, 3],
        "ground",
        "painted_steel",
      ),
      {
        ...element(
          "high_bay_lights",
          "light",
          "storage",
          [0.6, 0.25, 0.6],
          "ceiling",
          "emissive_fixture",
        ),
        count: 8,
      },
    ],
    relationships: [
      relationship("storage_racks", "on", "floor_slab"),
      relationship("service_route", "beside", "storage_racks"),
      relationship("service_route", "connected_to", "loading_bays"),
    ],
  },
  generic: {
    keywords: [],
    use_case: "purposeful built environment",
    style: "coherent context appropriate design",
    era: "contemporary",
    mood: "clear and believable",
    scale_references: [
      {
        name: "adult_person",
        size: [0.55, 1.75, 0.3],
        rationale: "human scale provides a universal reference",
      },
    ],
    focal_hierarchy: [
      "primary_focal_point",
      "main_route",
      "supporting_structure",
    ],
    palette: {
      dominant: ["context_neutral", "ground_material"],
      secondary: ["structural_material", "supporting_finish"],
      accent: ["single_focal_accent"],
    },
    material_vocabulary: [
      "context_surface",
      "structural_material",
      "painted_finish",
      "clear_glass",
      "accent_metal",
    ],
    lighting_scenario: {
      intent: "readable hierarchy with grounded shadows",
      min_lights: 2,
      max_lights: 8,
      require_shadows: true,
    },
    detail_budget: {
      hero: 1,
      supporting: 5,
      repeated: 10,
      micro_detail_density: "medium",
    },
    metric_rules: {
      human_clearance_m: [0.8, 1.5],
      primary_span_m: [3, 30],
      feature_height_m: [1, 12],
    },
    zones: [
      zone(
        "primary",
        "main activity area",
        [0, 2.5, 0],
        [12, 5, 12],
      ),
      zone(
        "circulation",
        "movement and access",
        [0, 0.05, 7],
        [3, 0.1, 8],
      ),
    ],
    elements: [
      element(
        "ground_surface",
        "surface",
        "primary",
        [12, 0.2, 12],
        "ground",
        "context_surface",
      ),
      element(
        "supporting_structure",
        "structure",
        "primary",
        [8, 5, 6],
        "ground",
        "structural_material",
      ),
      element(
        "primary_focal_point",
        "functional",
        "primary",
        [2, 2.5, 2],
        "surface",
        "accent_material",
      ),
      element(
        "main_route",
        "route",
        "circulation",
        [2, 0.05, 8],
        "surface",
        "route_finish",
      ),
      element(
        "supporting_detail",
        "detail",
        "primary",
        [1, 1, 1],
        "surface",
        "supporting_finish",
      ),
      {
        ...element(
          "key_lights",
          "light",
          "primary",
          [0.5, 0.5, 0.5],
          "none",
          "emissive_fixture",
        ),
        count: 2,
      },
    ],
    relationships: [
      relationship(
        "primary_focal_point",
        "on",
        "ground_surface",
      ),
      relationship(
        "main_route",
        "connected_to",
        "primary_focal_point",
      ),
      relationship(
        "supporting_detail",
        "beside",
        "primary_focal_point",
      ),
    ],
  },
};

export const design_template_names = Object.freeze(
  Object.keys(templates),
);

export const template_names = design_template_names;

function clone(value)
{
  return JSON.parse(JSON.stringify(value));
}

function normalized_name(value)
{
  return String(value ?? "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
}

function readable_prompt(value)
{
  return String(value ?? "")
    .trim()
    .toLowerCase()
    .replace(/[_-]+/g, " ");
}

function merge_object(base, overrides)
{
  const output = clone(base);
  for (const [key, value] of Object.entries(overrides ?? {}))
  {
    if (value !== undefined)
    {
      output[key] = clone(value);
    }
  }
  return output;
}

function add_issue(issues, path, code, message)
{
  issues.push({
    path,
    code,
    message,
  });
}

function valid_positive_vector3(value)
{
  return (
    Array.isArray(value) &&
    value.length === 3 &&
    value.every((entry) =>
      Number.isFinite(entry) && entry > 0,
    )
  );
}

function non_empty_array(value)
{
  return (
    Array.isArray(value) &&
    value.length > 0
  );
}

export function infer_design_template(prompt)
{
  const value = readable_prompt(prompt);
  let best_name = "generic";
  let best_score = 0;

  for (const name of design_template_names)
  {
    if (name === "generic")
    {
      continue;
    }

    const score = templates[name].keywords.reduce(
      (total, keyword) =>
        total + (
          value.includes(readable_prompt(keyword))
            ? readable_prompt(keyword).split(" ").length
            : 0
        ),
      0,
    );
    if (score > best_score)
    {
      best_name = name;
      best_score = score;
    }
  }

  return best_name;
}

export function validate_design_brief(brief)
{
  const errors = [];
  const warnings = [];
  const required_strings = [
    "use_case",
    "style",
    "era",
    "mood",
  ];

  if (!brief || typeof brief !== "object")
  {
    add_issue(
      errors,
      "",
      "invalid_brief",
      "brief must be an object",
    );
    return {
      valid: false,
      errors,
      warnings,
    };
  }

  if (!design_template_names.includes(brief.template_name))
  {
    add_issue(
      errors,
      "template_name",
      "unknown_template",
      "template_name is not supported",
    );
  }

  for (const field of required_strings)
  {
    if (!String(brief[field] ?? "").trim())
    {
      add_issue(
        errors,
        field,
        "missing_field",
        `${field} is required`,
      );
    }
  }

  const required_arrays = [
    "scale_references",
    "focal_hierarchy",
    "material_vocabulary",
  ];
  for (const field of required_arrays)
  {
    if (!non_empty_array(brief[field]))
    {
      add_issue(
        errors,
        field,
        "missing_field",
        `${field} requires at least one entry`,
      );
    }
  }

  for (
    const [index, reference] of
      (brief.scale_references ?? []).entries()
  )
  {
    if (
      !String(reference?.name ?? "").trim() ||
      !valid_positive_vector3(reference?.size) ||
      !String(reference?.rationale ?? "").trim()
    )
    {
      add_issue(
        errors,
        `scale_references.${index}`,
        "invalid_scale_reference",
        "scale reference requires name, size, and rationale",
      );
    }
  }

  if (
    !non_empty_array(brief.palette?.dominant) ||
    !non_empty_array(brief.palette?.secondary) ||
    !non_empty_array(brief.palette?.accent)
  )
  {
    add_issue(
      errors,
      "palette",
      "invalid_palette",
      "palette requires dominant, secondary, and accent colors",
    );
  }

  const accent_ratio =
    brief.palette_rules?.accent_ratio;
  if (
    !Array.isArray(accent_ratio) ||
    accent_ratio.length !== 2 ||
    !accent_ratio.every(Number.isFinite) ||
    accent_ratio[0] < 0 ||
    accent_ratio[1] > 0.2 ||
    accent_ratio[0] >= accent_ratio[1]
  )
  {
    add_issue(
      errors,
      "palette_rules.accent_ratio",
      "invalid_accent_ratio",
      "accent ratio must be an increasing range up to 0.2",
    );
  }

  if (
    !brief.palette_rules?.value_hierarchy ||
    !brief.palette_rules?.roughness_ranges ||
    !brief.palette_rules?.texture_scale ||
    !brief.palette_rules?.wear
  )
  {
    add_issue(
      errors,
      "palette_rules",
      "incomplete_pbr_rules",
      "value, roughness, texture scale, and wear rules are required",
    );
  }

  const lighting = brief.lighting_scenario;
  if (
    !String(lighting?.intent ?? "").trim() ||
    !Number.isInteger(lighting?.min_lights) ||
    lighting.min_lights < 1
  )
  {
    add_issue(
      errors,
      "lighting_scenario",
      "invalid_lighting",
      "lighting requires intent and at least one light",
    );
  }
  if (
    Number.isInteger(lighting?.max_lights) &&
    Number.isInteger(lighting?.min_lights) &&
    lighting.max_lights < lighting.min_lights
  )
  {
    add_issue(
      errors,
      "lighting_scenario.max_lights",
      "invalid_lighting_range",
      "max_lights cannot be below min_lights",
    );
  }

  const detail_budget = brief.detail_budget;
  if (
    !Number.isInteger(detail_budget?.hero) ||
    detail_budget.hero < 1 ||
    !Number.isInteger(detail_budget?.supporting) ||
    detail_budget.supporting < 1 ||
    !Number.isInteger(detail_budget?.repeated) ||
    detail_budget.repeated < 0
  )
  {
    add_issue(
      errors,
      "detail_budget",
      "invalid_detail_budget",
      "detail budget requires positive hero and supporting counts",
    );
  }

  if (
    !brief.metric_rules ||
    Object.keys(brief.metric_rules).length === 0
  )
  {
    add_issue(
      errors,
      "metric_rules",
      "missing_metric_rules",
      "at least one metric rule is required",
    );
  }

  if (
    non_empty_array(brief.focal_hierarchy) &&
    brief.focal_hierarchy.length > 5
  )
  {
    add_issue(
      warnings,
      "focal_hierarchy",
      "diffuse_hierarchy",
      "more than five focal levels may weaken hierarchy",
    );
  }

  return {
    valid: errors.length === 0,
    errors,
    warnings,
  };
}

export function create_design_brief(prompt, overrides = {})
{
  const requested_name = normalized_name(
    overrides.template_name,
  );
  const template_name = design_template_names.includes(
    requested_name,
  )
    ? requested_name
    : infer_design_template(prompt);
  const template = templates[template_name];
  const brief = merge_object(
    {
      template_name,
      prompt: String(prompt ?? "").trim(),
      use_case: template.use_case,
      style: template.style,
      era: template.era,
      mood: template.mood,
      scale_references: template.scale_references,
      focal_hierarchy: template.focal_hierarchy,
      palette: template.palette,
      material_vocabulary: template.material_vocabulary,
      lighting_scenario: template.lighting_scenario,
      detail_budget: template.detail_budget,
      metric_rules: template.metric_rules,
      palette_rules,
    },
    overrides,
  );

  brief.template_name = template_name;
  brief.validation = validate_design_brief(brief);
  return brief;
}

function semantic_tags_for(element, brief)
{
  const tags = new Set();
  const name = normalized_name(element.name);
  if (/\b(entry|entrance|door|threshold)\b/.test(
    name.replaceAll("_", " "),
  ))
  {
    tags.add("entrance");
  }
  if (element.role === "structure")
  {
    tags.add("support");
  }
  if (
    element.role === "surface" ||
    element.role === "route"
  )
  {
    tags.add("walkable");
  }
  if (
    element.role === "route" &&
    /\b(service|operations|loading|apron)\b/.test(
      name.replaceAll("_", " "),
    )
  )
  {
    tags.add("service_route");
  }
  if (
    normalized_name(brief.focal_hierarchy?.[0]) === name
  )
  {
    tags.add("focal_point");
  }
  return [...tags];
}

function material_semantic_for(value)
{
  const name = normalized_name(value);
  if (name.includes("glass"))
  {
    return "glass";
  }
  if (name.includes("wood"))
  {
    return "wood";
  }
  if (
    name.includes("fabric") ||
    name.includes("textile")
  )
  {
    return "fabric";
  }
  if (name.includes("concrete"))
  {
    return "concrete";
  }
  if (
    name.includes("asphalt") ||
    name.includes("tarmac")
  )
  {
    return "asphalt";
  }
  if (
    name.includes("masonry") ||
    name.includes("brick")
  )
  {
    return "masonry";
  }
  if (name.includes("rubber"))
  {
    return "rubber";
  }
  if (
    name.includes("marking") ||
    name.includes("road_paint") ||
    name.includes("lane")
  )
  {
    return "road_paint";
  }
  if (
    name.includes("emissive") ||
    name.includes("light") ||
    name.includes("sign")
  )
  {
    return "emissive";
  }
  if (
    name.includes("painted_metal") ||
    name.includes("painted_steel") ||
    name.includes("painted_aluminum")
  )
  {
    return "painted_metal";
  }
  if (
    name.includes("metal") ||
    name.includes("steel") ||
    name.includes("aluminum")
  )
  {
    return "metal";
  }
  if (
    name.includes("plaster") ||
    name.includes("wall") ||
    name.includes("paint")
  )
  {
    return "painted_wall";
  }
  return "paint";
}

export function suggest_scene_plan(brief)
{
  const validation = validate_design_brief(brief);
  if (!validation.valid)
  {
    return {
      ok: false,
      errors: validation.errors,
      warnings: validation.warnings,
    };
  }

  const template = templates[brief.template_name];
  const reference = brief.scale_references[0];
  const root_name = normalized_name(
    brief.root_name ||
    brief.use_case ||
    brief.template_name,
  );
  const material_vocabulary =
    brief.material_vocabulary;
  const elements = clone(template.elements)
    .map((entry, index) => ({
      ...entry,
      material_semantic: material_semantic_for(
        entry.material_semantic ??
        material_vocabulary[
          index % material_vocabulary.length
        ],
      ),
      semantic_tags: semantic_tags_for(
        entry,
        brief,
      ),
    }));

  return {
    root_name:
      root_name ||
      `${brief.template_name}_environment`,
    purpose: [
      brief.use_case,
      brief.style,
      brief.era,
      brief.mood,
    ].join(", "),
    scale_reference: {
      name: reference.name,
      size: clone(reference.size),
      rationale: reference.rationale,
    },
    zones: clone(template.zones),
    elements,
    relationships: clone(template.relationships),
    lighting: clone(brief.lighting_scenario),
    quality_goals: [
      `preserve ${brief.focal_hierarchy.join(" over ")}`,
      "maintain real world metric scale",
      "keep circulation and service routes clear",
      "apply wear only where use or exposure supports it",
      "keep accent coverage within the palette ratio",
    ],
  };
}

export const design_intelligence_suggestion =
  suggest_scene_plan;

export const scene_plan_suggestion =
  suggest_scene_plan;
