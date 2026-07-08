// intensity is lux for directional, lumens otherwise

export function calibrated_light_defaults(type)
{
  if (type === "area")
  {
    return {
      light_type: "area",
      color: [1.0, 0.93, 0.82, 1.0],
      temperature: 3200,
      intensity: 12000,
      range: 40,
      area_width: 6,
      area_height: 3,
      shadows: true,
      volumetric: false,
      draw_distance: 80,
      shadow_distance: 60,
    };
  }

  if (type === "spot")
  {
    return {
      light_type: "spot",
      color: [1.0, 0.94, 0.84, 1.0],
      temperature: 3500,
      intensity: 8500,
      range: 35,
      angle_degrees: 45,
      shadows: true,
      volumetric: false,
      draw_distance: 70,
      shadow_distance: 50,
    };
  }

  if (type === "directional")
  {
    return {
      light_type: "directional",
      color: [1.0, 0.96, 0.9, 1.0],
      temperature: 5500,
      intensity: 120000,
      shadows: true,
      volumetric: false,
    };
  }

  return {
    light_type: "point",
    color: [1.0, 0.92, 0.78, 1.0],
    temperature: 3200,
    intensity: 8500,
    range: 30,
    shadows: true,
    volumetric: false,
    draw_distance: 60,
    shadow_distance: 45,
  };
}

export function light_intensity_floor(type)
{
  if (type === "directional")
  {
    return 1000;
  }
  if (type === "area")
  {
    return 1600;
  }
  return 800;
}

function role_from_name(name)
{
  const value = String(name ?? "").toLowerCase().replace(/[_-]+/g, " ");
  if (/\b(brake|brakelight|tail light|rear light)\b/.test(value))
  {
    return "brake";
  }
  if (/\b(exhaust|muffler)\b/.test(value))
  {
    return "exhaust";
  }
  if (/\b(headlight|head light|low beam|high beam)\b/.test(value))
  {
    return "headlight";
  }
  if (/\b(sign|neon|billboard)\b/.test(value))
  {
    return "sign";
  }
  if (/\b(canopy|pump|gas station|gs )\b/.test(value) || value.includes("gs "))
  {
    return "canopy";
  }
  if (/\b(warehouse|area light|ceiling)\b/.test(value))
  {
    return "warehouse";
  }
  if (/\b(office|interior|room)\b/.test(value))
  {
    return "office";
  }
  if (/\b(crane|loading|dock|yard|pole|street|highway|road)\b/.test(value))
  {
    return "yard";
  }
  if (/\b(sun|directional|sky)\b/.test(value))
  {
    return "directional";
  }
  return "";
}

function role_defaults(role, light_type)
{
  if (role === "brake")
  {
    return {
      color: [1.0, 0.05, 0.02, 1.0],
      temperature: 1800,
      intensity: 180,
      range: 3.5,
      shadows: false,
      volumetric: false,
    };
  }
  if (role === "exhaust")
  {
    return {
      color: [1.0, 0.42, 0.08, 1.0],
      temperature: 2000,
      intensity: 90,
      range: 0.6,
      shadows: false,
      volumetric: false,
    };
  }
  if (role === "headlight")
  {
    return {
      color: [1.0, 0.96, 0.88, 1.0],
      temperature: 5000,
      intensity: 3200,
      range: 45,
      angle_degrees: 12,
      shadows: true,
      volumetric: true,
    };
  }
  if (role === "sign")
  {
    return {
      color: [1.0, 0.35, 0.12, 1.0],
      temperature: 2200,
      intensity: 4500,
      range: 18,
      shadows: false,
    };
  }
  if (role === "canopy")
  {
    return {
      color: [1.0, 0.94, 0.82, 1.0],
      temperature: 3500,
      intensity: 9000,
      range: 28,
      area_width: 8,
      area_height: 2,
      shadows: true,
    };
  }
  if (role === "warehouse")
  {
    return {
      color: [1.0, 0.95, 0.85, 1.0],
      temperature: 3200,
      intensity: 12000,
      range: 40,
      area_width: 12,
      area_height: 4,
      shadows: true,
    };
  }
  if (role === "office")
  {
    return {
      color: [1.0, 0.92, 0.78, 1.0],
      temperature: 3200,
      intensity: 2500,
      range: 12,
      shadows: false,
    };
  }
  if (role === "yard")
  {
    if (light_type === "spot")
    {
      return {
        color: [1.0, 0.94, 0.84, 1.0],
        temperature: 3500,
        intensity: 8500,
        range: 35,
        angle_degrees: 50,
        shadows: true,
      };
    }
    return {
      color: [1.0, 0.92, 0.75, 1.0],
      temperature: 3200,
      intensity: 8500,
      range: 35,
      shadows: true,
    };
  }
  if (role === "directional")
  {
    return calibrated_light_defaults("directional");
  }
  return null;
}

export function resolve_light_properties(args)
{
  const effective_light_type = args.light_type ?? "point";
  const defaults = calibrated_light_defaults(effective_light_type);
  if (args.calibrated === false)
  {
    return {
      light_type: args.light_type ?? defaults.light_type,
      color: args.color,
      temperature: args.temperature,
      intensity: args.intensity,
      range: args.range,
      angle_degrees: args.angle_degrees,
      area_width: args.area_width,
      area_height: args.area_height,
      shadows: args.shadows,
      volumetric: args.volumetric,
      draw_distance: args.draw_distance,
      shadow_distance: args.shadow_distance,
      volumetric_distance: args.volumetric_distance,
    };
  }

  const floor = light_intensity_floor(effective_light_type);
  const requested_intensity = args.intensity;
  const intensity = (requested_intensity === undefined || requested_intensity === null || Number(requested_intensity) < floor)
    ? defaults.intensity
    : requested_intensity;

  return {
    light_type: effective_light_type,
    color: args.color ?? defaults.color,
    temperature: args.temperature ?? defaults.temperature,
    intensity,
    range: args.range ?? defaults.range,
    angle_degrees: args.angle_degrees ?? defaults.angle_degrees,
    area_width: args.area_width ?? defaults.area_width,
    area_height: args.area_height ?? defaults.area_height,
    shadows: args.shadows ?? defaults.shadows,
    volumetric: args.volumetric ?? defaults.volumetric,
    draw_distance: args.draw_distance ?? defaults.draw_distance,
    shadow_distance: args.shadow_distance ?? defaults.shadow_distance,
    volumetric_distance: args.volumetric_distance ?? defaults.volumetric_distance,
  };
}

export function calibrate_existing_light(entity_name, properties)
{
  const light_type = String(properties?.light_type ?? "point").toLowerCase();
  const role = role_from_name(entity_name);
  const role_values = role_defaults(role, light_type);
  const type_defaults = calibrated_light_defaults(light_type);
  const base = role_values ? { ...type_defaults, ...role_values } : { ...type_defaults };

  // keep intentional specialty lights dim, otherwise lift weak blockout values
  const specialty = role === "brake" || role === "exhaust";
  const current_intensity = Number(properties?.intensity);
  if (!specialty && Number.isFinite(current_intensity) && current_intensity >= light_intensity_floor(light_type))
  {
    base.intensity = current_intensity;
  }
  if (!specialty && Number.isFinite(Number(properties?.range)) && Number(properties.range) > 0 && light_type !== "directional")
  {
    // keep an existing sensible range if the light already has one in the right ballpark
    const current_range = Number(properties.range);
    if (current_range >= (base.range ?? 0) * 0.5)
    {
      base.range = current_range;
    }
  }

  const updates = {};
  for (const key of ["color", "temperature", "intensity", "range", "angle_degrees", "area_width", "area_height", "shadows", "volumetric", "draw_distance", "shadow_distance"])
  {
    if (base[key] === undefined || base[key] === null)
    {
      continue;
    }
    updates[key] = base[key];
  }

  return {
    role: role || light_type,
    light_type,
    updates,
  };
}
