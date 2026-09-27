-- Copyright(c) 2015-2026 Panos Karabelas
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions :
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

-- endless liminal building generator
--
-- an infinite single storey floor streamed in 48 m chunks around the camera
-- every chunk belongs to a building program (backrooms, office, hotel, school, parking, mall, pool),
-- programs are picked from a jittered voronoi field over chunk coordinates, so they form
-- irregular districts and you cross from one building into another through a threshold
-- every chunk edge has one opening at its midpoint, its size is agreed by the two programs that
-- share the edge, so neighbours never coordinate beyond hashing each other's program
-- each chunk builds its own half of every border wall, the two faces can carry different finishes
-- geometry is deterministic per chunk, revisiting a place rebuilds it identically

local liminal = {}

-- serialized configuration, editable from the script node attributes in the world file
liminal.seed             = 0      -- 0 rolls a new layout each play, any other value locks it
liminal.district_chunks  = 3      -- average district size in chunks, one district is one building
liminal.view_radius      = 1      -- chunks that must exist around the camera
liminal.build_extra      = 1      -- rings built hidden ahead of the camera
liminal.keep_extra       = 1      -- rings kept behind so looking back does not despawn geometry
liminal.spawn_budget     = 160    -- entities spawned per frame while streaming
liminal.stream_interval  = 0.1    -- seconds between residency checks
liminal.light_count      = 10     -- real point lights that follow the camera
liminal.light_volumetric = true
liminal.flicker          = true
liminal.stalker          = true   -- the entity that hunts the building
liminal.soundscape       = true   -- room tone per program and the sounds the building makes out of sight

local CHUNK = 48.0
local HALF  = 24.0
local WALL  = 0.15                  -- each chunk's half of a border wall

local GEN = "project/liminal_space_resources/"
local TEX = "project/materials/"

-- materials are either authored files, texture sets from the project, or flat colors
-- texture sets use world space uvs, tiling is repeats per meter
local material_defs =
{
    br_wallpaper     = { file = "project/liminal_space_resources/wallpaper.xml" },
    br_carpet        = { file = "project/liminal_space_resources/liminal_carpet.xml" },
    br_ceiling       = { file = "project/liminal_space_resources/ceiling_tiles.xml" },
    light_panel      = { file = "project/liminal_space_resources/ceiling_light.xml" },
    diffuser_dead    = { file = "project/liminal_space_resources/diffuser_dead.xml" },
    diffuser_dim     = { file = "project/liminal_space_resources/diffuser_dim.xml" },
    fixture_frame    = { file = "project/liminal_space_resources/fixture_frame.xml" },
    vent_shadow      = { file = "project/liminal_space_resources/vent_shadow.xml" },
    wall_patch       = { file = "project/liminal_space_resources/wall_patch.xml" },
    replacement_tile = { file = "project/liminal_space_resources/replacement_tile.xml" },

    hotel_carpet     = { file = GEN .. "liminal_hotel_carpet.xml",    fallback = { color = { 0.24, 0.06, 0.08 }, roughness = 0.95 } },
    hotel_wallpaper  = { file = GEN .. "liminal_hotel_wallpaper.xml", fallback = { color = { 0.78, 0.70, 0.55 }, roughness = 0.8 } },
    wood_panel       = { file = GEN .. "liminal_wood.xml",            fallback = { color = { 0.30, 0.17, 0.09 }, roughness = 0.5 } },
    terrazzo         = { file = GEN .. "liminal_terrazzo.xml",        fallback = { color = { 0.78, 0.75, 0.70 }, roughness = 0.2 } },
    lockers          = { file = GEN .. "liminal_lockers.xml",         fallback = { color = { 0.25, 0.38, 0.52 }, roughness = 0.45, metalness = 0.6 } },
    shutter          = { file = GEN .. "liminal_shutter.xml",         fallback = { color = { 0.55, 0.56, 0.57 }, roughness = 0.4, metalness = 0.8 } },
    lino             = { file = GEN .. "liminal_lino.xml",            fallback = { color = { 0.72, 0.70, 0.62 }, roughness = 0.35 } },
    office_carpet    = { file = GEN .. "liminal_office_carpet.xml",   fallback = { color = { 0.28, 0.31, 0.36 }, roughness = 0.95 } },
    hazard           = { file = GEN .. "liminal_hazard.xml",          fallback = { color = { 0.85, 0.65, 0.05 }, roughness = 0.6 } },
    exit_green       = { file = GEN .. "liminal_exit_sign.xml",       fallback = { color = { 0.05, 0.9, 0.25 }, emissive = true } },
    sign_a           = { file = GEN .. "liminal_sign_a.xml",          fallback = { color = { 0.9, 0.9, 0.85 }, emissive = true } },
    sign_b           = { file = GEN .. "liminal_sign_b.xml",          fallback = { color = { 0.9, 0.2, 0.2 }, emissive = true } },
    sign_c           = { file = GEN .. "liminal_sign_c.xml",          fallback = { color = { 0.2, 0.5, 0.9 }, emissive = true } },
    sign_d           = { file = GEN .. "liminal_sign_d.xml",          fallback = { color = { 0.9, 0.7, 0.2 }, emissive = true } },
    sign_e           = { file = GEN .. "liminal_sign_e.xml",          fallback = { color = { 0.8, 0.3, 0.8 }, emissive = true } },
    level_sign       = { file = GEN .. "liminal_level_sign.xml",      fallback = { color = { 0.9, 0.75, 0.1 } } },

    ceiling_white    = { dir = TEX .. "backrooms/ceiling_tiles_2", ext = "png", tiling = 1 / 1.2, color = { 0.95, 0.95, 0.93 } },
    tile_white       = { dir = TEX .. "tile_white", ext = "png", tiling = 1 / 2.25 },
    tile_floor       = { dir = TEX .. "tile_modern", ext = "png", tiling = 1 / 2.4 },
    concrete         = { dir = TEX .. "concrete", ext = "jpg", tiling = 0.25, color = { 0.78, 0.78, 0.76 } },
    concrete_floor   = { dir = TEX .. "concrete", ext = "jpg", tiling = 0.2, color = { 0.52, 0.52, 0.5 } },
    pool_tile        = { dir = TEX .. "backrooms/pool_tiles", ext = "png", tiling = 1.0, color = { 0.95, 0.97, 0.97 } },
    pool_tile_aqua   = { dir = TEX .. "backrooms/pool_tiles", ext = "png", tiling = 1.0, color = { 0.42, 0.78, 0.86 } },
    pool_tile_pale   = { dir = TEX .. "backrooms/pool_tiles", ext = "png", tiling = 1.0, color = { 0.56, 0.86, 0.93 } },
    pool_tile_band   = { dir = TEX .. "backrooms/pool_tiles", ext = "png", tiling = 1.0, color = { 0.10, 0.30, 0.55 } },
    pool_deck        = { dir = TEX .. "tile_white", ext = "png", tiling = 1 / 2.25, color = { 0.80, 0.85, 0.87 } },
    pool_water       = { color = { 0.6, 0.9, 0.95, 0.85 }, roughness = 0.03, water = true },

    paint_white      = { color = { 0.86, 0.86, 0.84 }, roughness = 0.7 },
    office_paint     = { color = { 0.80, 0.78, 0.72 }, roughness = 0.85 },
    school_paint     = { color = { 0.80, 0.82, 0.72 }, roughness = 0.75 },
    mall_wall        = { color = { 0.84, 0.82, 0.78 }, roughness = 0.6 },
    mall_ceiling     = { color = { 0.9, 0.9, 0.88 }, roughness = 0.9 },
    hotel_ceiling    = { color = { 0.88, 0.85, 0.78 }, roughness = 0.9 },
    garage_paint     = { color = { 0.42, 0.50, 0.46 }, roughness = 0.8 },
    trim_door        = { color = { 0.55, 0.56, 0.55 }, roughness = 0.5, metalness = 0.3 },
    skirting_dark    = { color = { 0.12, 0.12, 0.13 }, roughness = 0.6 },
    wood_dark        = { color = { 0.18, 0.10, 0.06 }, roughness = 0.45 },
    wood_light       = { color = { 0.62, 0.46, 0.30 }, roughness = 0.55 },
    hotel_door       = { color = { 0.30, 0.17, 0.10 }, roughness = 0.4 },
    school_door      = { color = { 0.56, 0.40, 0.24 }, roughness = 0.5 },
    door_fire        = { color = { 0.46, 0.50, 0.46 }, roughness = 0.45, metalness = 0.5 },
    brass            = { color = { 0.78, 0.60, 0.30 }, roughness = 0.25, metalness = 1.0 },
    chrome           = { color = { 0.9, 0.9, 0.9 }, roughness = 0.08, metalness = 1.0 },
    steel            = { color = { 0.6, 0.62, 0.64 }, roughness = 0.35, metalness = 1.0 },
    elevator_metal   = { color = { 0.7, 0.7, 0.72 }, roughness = 0.2, metalness = 1.0 },
    mirror           = { color = { 0.95, 0.95, 0.95 }, roughness = 0.02, metalness = 1.0 },
    black_plastic    = { color = { 0.03, 0.03, 0.03 }, roughness = 0.4 },
    screen_dead      = { color = { 0.01, 0.01, 0.012 }, roughness = 0.08 },
    glass_clear      = { color = { 0.85, 0.92, 0.95, 0.24 }, roughness = 0.02 },
    glass_frost      = { color = { 0.9, 0.93, 0.94, 0.75 }, roughness = 0.55 },
    glass_dark       = { color = { 0.08, 0.09, 0.1, 0.75 }, roughness = 0.03 },
    fabric_panel     = { color = { 0.36, 0.40, 0.46 }, roughness = 1.0 },
    fabric_chair     = { color = { 0.42, 0.30, 0.22 }, roughness = 1.0 },
    curtain          = { color = { 0.55, 0.45, 0.30 }, roughness = 1.0 },
    linen            = { color = { 0.92, 0.91, 0.88 }, roughness = 0.9 },
    bed_cover        = { color = { 0.45, 0.14, 0.16 }, roughness = 0.95 },
    bed_base         = { color = { 0.2, 0.18, 0.17 }, roughness = 0.9 },
    room_carpet      = { color = { 0.58, 0.50, 0.40 }, roughness = 1.0 },
    carpet_wet       = { color = { 0.10, 0.08, 0.04, 1 }, roughness = 0.35 },
    cabinet          = { color = { 0.74, 0.72, 0.66 }, roughness = 0.5 },
    counter          = { color = { 0.30, 0.30, 0.32 }, roughness = 0.3 },
    appliance_white  = { color = { 0.9, 0.9, 0.88 }, roughness = 0.3 },
    vending_body     = { color = { 0.55, 0.06, 0.06 }, roughness = 0.35, metalness = 0.4 },
    water_bottle     = { color = { 0.45, 0.7, 0.9, 0.4 }, roughness = 0.05 },
    cardboard        = { color = { 0.55, 0.40, 0.24 }, roughness = 0.95 },
    whiteboard       = { color = { 0.95, 0.95, 0.95 }, roughness = 0.1 },
    chalkboard       = { color = { 0.07, 0.16, 0.12 }, roughness = 0.9 },
    cork             = { color = { 0.58, 0.42, 0.26 }, roughness = 1.0 },
    paper            = { color = { 0.95, 0.94, 0.9 }, roughness = 0.9 },
    desk_top         = { color = { 0.66, 0.54, 0.38 }, roughness = 0.5 },
    desk_frame       = { color = { 0.2, 0.22, 0.24 }, roughness = 0.4, metalness = 0.7 },
    chair_plastic    = { color = { 0.16, 0.32, 0.52 }, roughness = 0.4 },
    void_black       = { color = { 0.0, 0.0, 0.0 }, roughness = 1.0 },
    paint_line       = { color = { 0.88, 0.88, 0.82 }, roughness = 0.6 },
    paint_yellow     = { color = { 0.85, 0.66, 0.08 }, roughness = 0.6 },
    pipe_red         = { color = { 0.55, 0.05, 0.04 }, roughness = 0.4, metalness = 0.3 },
    cone_orange      = { color = { 0.95, 0.35, 0.05 }, roughness = 0.5 },
    zone_blue        = { color = { 0.10, 0.30, 0.70 }, roughness = 0.6 },
    zone_green       = { color = { 0.10, 0.50, 0.25 }, roughness = 0.6 },
    zone_red         = { color = { 0.65, 0.10, 0.10 }, roughness = 0.6 },
    zone_violet      = { color = { 0.40, 0.18, 0.55 }, roughness = 0.6 },
    planter          = { color = { 0.25, 0.24, 0.22 }, roughness = 0.6 },
    foliage          = { color = { 0.10, 0.25, 0.08 }, roughness = 0.8 },
    mannequin        = { color = { 0.9, 0.88, 0.85 }, roughness = 0.25 },
    garment_a        = { color = { 0.5, 0.1, 0.12 }, roughness = 1.0 },
    garment_b        = { color = { 0.12, 0.2, 0.35 }, roughness = 1.0 },
    garment_c        = { color = { 0.7, 0.66, 0.55 }, roughness = 1.0 },
    metal_tread      = { color = { 0.35, 0.36, 0.38 }, roughness = 0.35, metalness = 0.9 },
    sign_dead        = { color = { 0.12, 0.12, 0.13 }, roughness = 0.3 },
    water_placeholder = { color = { 0.2, 0.45, 0.55, 0.55 }, roughness = 0.02 },

    shade_warm       = { color = { 1.0, 0.78, 0.52 }, emissive = true },
    shade_dim        = { color = { 0.25, 0.18, 0.11 }, emissive = true },
    shade_off        = { color = { 0.62, 0.55, 0.45 }, roughness = 0.9 },
    dome_warm        = { color = { 1.0, 0.86, 0.66 }, emissive = true },
    panel_cool       = { color = { 0.92, 0.97, 1.0 }, emissive = true },
    tube_cool        = { color = { 0.9, 0.97, 1.0 }, emissive = true },
    tube_off         = { color = { 0.5, 0.5, 0.5 }, roughness = 0.3 },
    tube_dim         = { color = { 0.2, 0.22, 0.24 }, emissive = true },
    sodium_tube      = { color = { 1.0, 0.55, 0.16 }, emissive = true },
    sodium_dim       = { color = { 0.3, 0.14, 0.03 }, emissive = true },
    downlight        = { color = { 1.0, 0.9, 0.75 }, emissive = true },
    sky_panel        = { color = { 0.82, 0.9, 1.0 }, emissive = true },
    window_overcast  = { color = { 0.9, 0.93, 0.96 }, emissive = true },
    vending_glow     = { color = { 0.85, 0.92, 1.0 }, emissive = true },
    button_glow      = { color = { 1.0, 0.7, 0.3 }, emissive = true },
    indicator_amber  = { color = { 1.0, 0.45, 0.05 }, emissive = true },
    menu_glow        = { color = { 1.0, 0.95, 0.8 }, emissive = true },
    pool_light       = { color = { 0.75, 0.95, 1.0 }, emissive = true },
    lane_blue        = { color = { 0.05, 0.12, 0.32 }, roughness = 0.2 },
    coping           = { color = { 0.90, 0.90, 0.87 }, roughness = 0.35 },
    rope_red         = { color = { 0.80, 0.08, 0.08 }, roughness = 0.5 },
    rope_white       = { color = { 0.90, 0.90, 0.90 }, roughness = 0.5 },
    rope_blue        = { color = { 0.08, 0.25, 0.75 }, roughness = 0.5 },
    lifeguard_white  = { color = { 0.92, 0.92, 0.90 }, roughness = 0.4 },
}

-- light fixture archetypes, on/off/dim are emissive materials, the rest drives pooled point lights
local fixtures =
{
    backrooms = { on = "light_panel", off = "diffuser_dead", dim = "diffuser_dim", temp = 3800, lumens = 1600, range = 10, dead = 0.06, dark_zone = 0.18, dim_zone = 0.16 },
    troffer   = { on = "light_panel", off = "diffuser_dead", dim = "diffuser_dim", temp = 4300, lumens = 1500, range = 9,  dead = 0.05, dark_zone = 0.12, dim_zone = 0.12 },
    sconce    = { on = "shade_warm",  off = "shade_off",     dim = "shade_dim",    temp = 2500, lumens = 420,  range = 5.5, dead = 0.10, dark_zone = 0.12, dim_zone = 0.1 },
    dome      = { on = "dome_warm",   off = "shade_off",     dim = "shade_dim",    temp = 2800, lumens = 750,  range = 7,  dead = 0.08, dark_zone = 0.12, dim_zone = 0.1 },
    lamp      = { on = "shade_warm",  off = "shade_off",     dim = "shade_dim",    temp = 2400, lumens = 320,  range = 4.5, dead = 0.2, dark_zone = 0, dim_zone = 0 },
    school    = { on = "panel_cool",  off = "tube_off",      dim = "tube_dim",     temp = 5200, lumens = 1700, range = 9,  dead = 0.07, dark_zone = 0.14, dim_zone = 0.12 },
    batten    = { on = "tube_cool",   off = "tube_off",      dim = "tube_dim",     temp = 4600, lumens = 1500, range = 10, dead = 0.18, dark_zone = 0.22, dim_zone = 0.15 },
    sodium    = { on = "sodium_tube", off = "tube_off",      dim = "sodium_dim",   temp = 2000, lumens = 1900, range = 11, dead = 0.16, dark_zone = 0.22, dim_zone = 0.15 },
    downlight = { on = "downlight",   off = "sign_dead",     dim = "shade_dim",    temp = 3400, lumens = 800,  range = 8,  dead = 0.08, dark_zone = 0.1, dim_zone = 0.1 },
    skylight  = { on = "sky_panel",   off = "sky_panel",     dim = "sky_panel",    temp = 6800, lumens = 3600, range = 16, dead = 0.0, dark_zone = 0, dim_zone = 0, steady = true },
    window    = { on = "window_overcast", off = "window_overcast", dim = "window_overcast", temp = 7000, lumens = 900, range = 6, dead = 0.0, dark_zone = 0, dim_zone = 0, steady = true },
    vending   = { on = "vending_glow", off = "screen_dead",  dim = "tube_dim",     temp = 6500, lumens = 260,  range = 3.5, dead = 0.15, dark_zone = 0, dim_zone = 0 },
    shop      = { on = "panel_cool",  off = "tube_off",      dim = "tube_dim",     temp = 4000, lumens = 1300, range = 9,  dead = 0.5,  dark_zone = 0, dim_zone = 0 },
    underwater = { on = "pool_light", off = "tube_off",      dim = "tube_dim",     temp = 9000, lumens = 900,  range = 7,  dead = 0.06, dark_zone = 0, dim_zone = 0 },
    hall      = { on = "panel_cool",  off = "tube_off",      dim = "tube_dim",     temp = 5200, lumens = 2600, range = 14, dead = 0.08, dark_zone = 0.08, dim_zone = 0.1 },
    poolroom  = { on = "panel_cool",  off = "tube_off",      dim = "tube_dim",     temp = 6200, lumens = 1400, range = 10, dead = 0.04, dark_zone = 0.05, dim_zone = 0.08 },
}

-- runtime state lives in file locals, anything on the script table gets serialized into the world file
local host            = nil
local initialized     = false
local materials       = {}
local chunks          = {}
local light_pool      = {}
local stream_timer    = 0.0
local light_timer     = 0.0
local flicker_timer   = 0.0
local layout_seed     = 1
local last_ccx        = nil
local last_ccz        = nil
local templates       = {}
local hum_audio       = nil
local footsteps_audio = nil
local hum_volume      = 0.07
local hum_pitch       = 0.97
local acoustic_size, acoustic_decay, acoustic_wet = 0.45, 0.36, 0.16
local program_cache   = {}
local stalker         = nil
local stalker_error   = nil
local stalker_api     = nil
local soundscape      = nil
local soundscape_error = nil
local soundscape_api  = nil
local door_voices     = {}
local door_voice_next = 1

-- fnv1a over three integers, the only source of determinism that does not depend on visit order
local function hash_u32(a, b, c)
    local h = 2166136261

    local function mix(v)
        v = math.floor(v) & 0xffffffff
        for i = 0, 3 do
            h = ((h ~ ((v >> (i * 8)) & 0xff)) * 16777619) & 0xffffffff
        end
    end

    mix(a)
    mix(b)
    mix(c)
    mix(layout_seed)

    return h
end

local function hash_unit(a, b, c)
    return (hash_u32(a, b, c) % 100000) / 100000.0
end

-- xorshift32, seeded per chunk so a chunk always regenerates the same way
local function prng_new(seed)
    local state = seed & 0xffffffff
    if state == 0 then
        state = 0x9e3779b9
    end

    return function()
        state = (state ~ ((state << 13) & 0xffffffff)) & 0xffffffff
        state = state ~ (state >> 17)
        state = (state ~ ((state << 5) & 0xffffffff)) & 0xffffffff
        return state / 4294967296.0
    end
end

local function chunk_key(cx, cz)
    return cx .. ":" .. cz
end

--------------------------------------------------------------------------------
-- builder primitives
-- builders work in chunk local meters, x and z in [0, 48], y up from the floor
-- a quadrant mirror lets one canonical south west quadrant builder serve all four quadrants
--------------------------------------------------------------------------------

local function mapx(ctx, x)
    return ctx.fx > 0 and x or CHUNK - x
end

local function mapz(ctx, z)
    return ctx.fz > 0 and z or CHUNK - z
end

-- quadrant 0 is the whole chunk, 1..4 mirror the canonical south west quadrant
local function set_quadrant(ctx, q)
    local mirrors = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } }
    local m = mirrors[q] or mirrors[1]
    ctx.fx, ctx.fz = m[1], m[2]
    ctx.q = q
end

-- the chunk edge that lies on the canonical west or south side of the current quadrant
local function canonical_edge(ctx, side)
    if side == "W" then
        return ctx.fx > 0 and ctx.edges.W or ctx.edges.E
    end
    return ctx.fz > 0 and ctx.edges.S or ctx.edges.N
end

local function push(ctx, job)
    ctx.jobs[#ctx.jobs + 1] = job
    return job
end

local function aabb(ctx, mat, x0, y0, z0, x1, y1, z1, physics, label)
    local ax0, ax1 = mapx(ctx, x0), mapx(ctx, x1)
    local az0, az1 = mapz(ctx, z0), mapz(ctx, z1)
    if ax1 < ax0 then ax0, ax1 = ax1, ax0 end
    if az1 < az0 then az0, az1 = az1, az0 end
    if y1 < y0 then y0, y1 = y1, y0 end
    local sx, sy, sz = ax1 - ax0, y1 - y0, az1 - az0
    if sx < 0.002 or sy < 0.002 or sz < 0.002 then
        return nil
    end
    return push(ctx,
    {
        material = mat, physics = physics, label = label,
        px = ctx.ox + (ax0 + ax1) * 0.5, py = (y0 + y1) * 0.5, pz = ctx.oz + (az0 + az1) * 0.5,
        sx = sx, sy = sy, sz = sz,
    })
end

-- mirrors turn a yaw into its reflection, boxes are symmetric so one flip covers both axes
local function mirrored_yaw(ctx, yaw)
    if ctx.fx * ctx.fz < 0 then
        return -yaw
    end
    return yaw
end

local function rbox(ctx, mat, x, y, z, sx, sy, sz, yaw, physics, label)
    return push(ctx,
    {
        material = mat, physics = physics, label = label,
        px = ctx.ox + mapx(ctx, x), py = y, pz = ctx.oz + mapz(ctx, z),
        sx = sx, sy = sy, sz = sz, yaw = mirrored_yaw(ctx, yaw or 0),
    })
end

-- a box tilted about the canonical z axis, used for ramps and escalator balustrades rising along x
local function slope_x(ctx, mat, x0, x1, y0, y1, z0, z1, thickness, physics, label)
    local run, rise = x1 - x0, y1 - y0
    local length = math.sqrt(run * run + rise * rise)
    local angle = math.deg(math.atan(rise, run)) * ctx.fx
    local job = push(ctx,
    {
        material = mat, physics = physics, label = label,
        px = ctx.ox + mapx(ctx, (x0 + x1) * 0.5), py = (y0 + y1) * 0.5, pz = ctx.oz + (mapz(ctx, z0) + mapz(ctx, z1)) * 0.5,
        sx = length, sy = thickness, sz = math.abs(z1 - z0), roll = angle,
    })
    return job
end

-- built in cylinder is radius 1 and height 1, centered
local function cyl(ctx, mat, x, y0, z, radius, height, physics, label)
    return push(ctx,
    {
        material = mat, physics = physics, label = label, mesh = MeshType.Cylinder,
        px = ctx.ox + mapx(ctx, x), py = y0 + height * 0.5, pz = ctx.oz + mapz(ctx, z),
        sx = radius, sy = height, sz = radius,
    })
end

local function sphere(ctx, mat, x, y, z, radius, label)
    return push(ctx,
    {
        material = mat, label = label, mesh = MeshType.Sphere,
        px = ctx.ox + mapx(ctx, x), py = y, pz = ctx.oz + mapz(ctx, z),
        sx = radius, sy = radius, sz = radius,
    })
end

-- prefab clones, yaw faces the prefab's local +z, mirrors reflect the facing
local function clone(ctx, template, x, z, yaw, y)
    local ya = yaw or 0
    if ctx.fx < 0 and ctx.fz > 0 then ya = -ya end
    if ctx.fx > 0 and ctx.fz < 0 then ya = 180 - ya end
    if ctx.fx < 0 and ctx.fz < 0 then ya = 180 + ya end
    ctx.clones[#ctx.clones + 1] = { template = template, px = ctx.ox + mapx(ctx, x), py = y or 0, pz = ctx.oz + mapz(ctx, z), yaw = ya }
end

-- burnt out lights clump into dark stretches instead of speckle, zones are 32 m in world space
local function fixture_alive(spec, px, pz, salt)
    local field = hash_unit(math.floor(px / 32), math.floor(pz / 32), 0xd0)
    local dead = spec.dead
    if field < spec.dark_zone then
        dead = 0.9
    elseif field < spec.dark_zone + spec.dim_zone then
        dead = 0.45
    end
    return hash_unit(math.floor(px * 4), math.floor(pz * 4), salt or 0x33) >= dead
end

-- turns an emissive job into a light fixture, a live one feeds the pooled point lights
local function make_light(ctx, job, spec, drop, salt)
    if not job then
        return nil
    end
    local alive = fixture_alive(spec, job.px, job.pz, salt)
    job.material = alive and spec.on or spec.off
    if not alive then
        return nil
    end
    local fixture =
    {
        job.px, job.pz, job.py - (drop or 0.25),
        spec = spec, gx = math.floor(job.px * 4), gz = math.floor(job.pz * 4), level = 1,
    }
    job.fixture = fixture
    ctx.fixtures[#ctx.fixtures + 1] = fixture
    return fixture
end

-- intervals of a wall line that openings leave free, floor_only ignores raised windows
local function free_spans(a0, a1, openings, floor_only)
    local ops = {}
    for _, o in ipairs(openings or {}) do
        if not floor_only or not o.sill or o.sill <= 0.01 then
            ops[#ops + 1] = o
        end
    end
    table.sort(ops, function(p, q) return p.a < q.a end)
    local list = {}
    local cur = a0
    for _, o in ipairs(ops) do
        if o.a > cur then
            list[#list + 1] = { cur, math.min(o.a, a1) }
        end
        cur = math.max(cur, o.b)
    end
    if cur < a1 then
        list[#list + 1] = { cur, a1 }
    end
    return list
end

-- a straight wall, axis "x" runs along x at z = line, axis "z" runs along z at x = line
-- openings are { a, b, top, sill } in along coordinates, top above y1 leaves no lintel
local function wall(ctx, mat, axis, line, t, a0, a1, y0, y1, openings, label)
    local function piece(a, b, ya, yb)
        if b - a < 0.01 or yb - ya < 0.01 then
            return
        end
        if axis == "x" then
            aabb(ctx, mat, a, ya, line - t * 0.5, b, yb, line + t * 0.5, true, label or "wall")
        else
            aabb(ctx, mat, line - t * 0.5, ya, a, line + t * 0.5, yb, b, true, label or "wall")
        end
    end

    local ops = {}
    for _, o in ipairs(openings or {}) do
        ops[#ops + 1] = o
    end
    table.sort(ops, function(p, q) return p.a < q.a end)

    local cur = a0
    for _, o in ipairs(ops) do
        local oa, ob = math.max(o.a, a0), math.min(o.b, a1)
        if ob > oa then
            if oa > cur then
                piece(cur, oa, y0, y1)
            end
            local top = math.min(o.top or y1, y1)
            if y1 - top > 0.02 then
                piece(oa, ob, top, y1)
            end
            if o.sill and o.sill > y0 + 0.01 then
                piece(oa, ob, y0, o.sill)
            end
            cur = math.max(cur, ob)
        end
    end
    if cur < a1 then
        piece(cur, a1, y0, y1)
    end
end

-- a thin layer on a wall face, dir is the side of the face the layer grows toward
local function face_box(ctx, mat, axis, face, dir, a, b, y0, y1, depth, physics, label)
    if axis == "x" then
        return aabb(ctx, mat, a, y0, face, b, y1, face + dir * depth, physics, label)
    end
    return aabb(ctx, mat, face, y0, a, face + dir * depth, y1, b, physics, label)
end

local function casing(ctx, mat, axis, face, dir, a, b, top, width, depth, sill)
    width = width or 0.07
    depth = depth or 0.02
    local y0 = sill or 0
    face_box(ctx, mat, axis, face, dir, a - width, a, y0, top + width, depth, false, "casing")
    face_box(ctx, mat, axis, face, dir, b, b + width, y0, top + width, depth, false, "casing")
    face_box(ctx, mat, axis, face, dir, a, b, top, top + width, depth, false, "casing")
    if sill then
        face_box(ctx, mat, axis, face, dir, a - width, b + width, sill - width, sill, depth * 1.6, false, "sill")
    end
end

local function skirting(ctx, mat, axis, face, dir, a0, a1, openings, height)
    for _, s in ipairs(free_spans(a0, a1, openings, true)) do
        face_box(ctx, mat, axis, face, dir, s[1], s[2], 0, height or 0.1, 0.015, false, "skirting")
    end
end

-- a leaf that swings into the side dir, face is the wall face on that side
-- a closed leaf sits flush inside the reveal, an open one rotates about its hinge
-- canonical centre and yaw of a leaf at any swing angle, mapped through the quadrant mirror of its chunk
local function door_pose(d, angle)
    local th = math.rad(angle)
    local ca, cn = d.toward * math.cos(th), d.dir * math.sin(th)
    local ha, hn = d.hinge, d.face - d.dir * d.thick * 0.5
    local ma, mn = ha + ca * d.width * 0.5, hn + cn * d.width * 0.5
    local dx, dz, px, pz
    if d.axis == "x" then
        dx, dz, px, pz = ca, cn, ma, mn
    else
        dx, dz, px, pz = cn, ca, mn, ma
    end
    local yaw = math.deg(math.atan(-dz, dx))
    return d.ox + mapx(d, px), d.oz + mapz(d, pz), mirrored_yaw(d, yaw)
end

local function door_leaf(ctx, mat, axis, face, dir, hinge, toward, width, height, thick, angle)
    local d =
    {
        ox = ctx.ox, oz = ctx.oz, fx = ctx.fx, fz = ctx.fz,
        axis = axis, face = face, dir = dir, hinge = hinge, toward = toward, width = width, height = height, thick = thick,
        angle = angle, target = angle, open_angle = angle > 30 and angle or 85, initial_open = angle > 30,
    }
    d.open = d.initial_open
    local px, pz, yaw = door_pose(d, angle)
    d.cx, d.cz, d.closed_yaw = door_pose(d, 0)
    local job = push(ctx,
    {
        material = mat, physics = true, label = "door",
        px = px, py = height * 0.5, pz = pz, sx = width, sy = height, sz = thick, yaw = yaw, door = d,
    })
    ctx.doors = ctx.doors or {}
    ctx.doors[#ctx.doors + 1] = d
    return job
end

local function floor_slab(ctx, mat)
    aabb(ctx, mat, 0, -0.3, 0, CHUNK, 0, CHUNK, true, "floor")
end

local function ceiling_slab(ctx, mat, h)
    aabb(ctx, mat, 0, h, 0, CHUNK, h + 0.25, CHUNK, false, "ceiling")
end

-- rectangles minus holes, merged into horizontal strips, used for skylights and ramp wells
local function slab_with_holes(ctx, mat, y0, y1, x0, z0, x1, z1, holes, physics)
    local xs, zs = { x0, x1 }, { z0, z1 }
    for _, h in ipairs(holes) do
        xs[#xs + 1] = math.max(x0, math.min(x1, h[1]))
        xs[#xs + 1] = math.max(x0, math.min(x1, h[3]))
        zs[#zs + 1] = math.max(z0, math.min(z1, h[2]))
        zs[#zs + 1] = math.max(z0, math.min(z1, h[4]))
    end
    table.sort(xs)
    table.sort(zs)
    local function in_hole(x, z)
        for _, h in ipairs(holes) do
            if x > h[1] and x < h[3] and z > h[2] and z < h[4] then
                return true
            end
        end
        return false
    end
    for j = 1, #zs - 1 do
        local za, zb = zs[j], zs[j + 1]
        if zb - za > 0.01 then
            local run_start = nil
            for i = 1, #xs - 1 do
                local xa, xb = xs[i], xs[i + 1]
                local solid = xb - xa > 0.0001 and not in_hole((xa + xb) * 0.5, (za + zb) * 0.5)
                if solid and not run_start then
                    run_start = xa
                end
                if (not solid or i == #xs - 1) and run_start then
                    local run_end = solid and xb or xa
                    aabb(ctx, mat, run_start, y0, za, run_end, y1, zb, physics, "slab")
                    run_start = nil
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- programs and the district field
--------------------------------------------------------------------------------

local P = {}
local program_list = {}

local function register(name, def)
    def.name = name
    P[name] = def
    program_list[#program_list + 1] = def
    def.order = #program_list
end

-- jittered voronoi over chunk coordinates, districts get irregular outlines
local function program_at(cx, cz)
    local key = chunk_key(cx, cz)
    local cached = program_cache[key]
    if cached then
        return cached
    end

    local span = math.max(1, liminal.district_chunks)
    local zx, zz = math.floor(cx / span), math.floor(cz / span)
    local best, bx, bz = math.huge, zx, zz
    for dz = -1, 1 do
        for dx = -1, 1 do
            local sx, sz = zx + dx, zz + dz
            local px = (sx + 0.15 + hash_unit(sx, sz, 0x100) * 0.7) * span
            local pz = (sz + 0.15 + hash_unit(sx, sz, 0x101) * 0.7) * span
            local ddx, ddz = cx + 0.5 - px, cz + 0.5 - pz
            local d = ddx * ddx + ddz * ddz
            if d < best then
                best, bx, bz = d, sx, sz
            end
        end
    end

    local start_x, start_z = math.floor(0 / span), math.floor(0 / span)
    local program
    local total = 0
    for _, p in ipairs(program_list) do total = total + p.weight end
    local roll = hash_unit(bx, bz, 0x102) * total
    for _, p in ipairs(program_list) do
        roll = roll - p.weight
        if roll <= 0 then
            program = p
            break
        end
    end
    program = program or program_list[1]

    -- the walk always begins in the familiar yellow rooms
    local sbest, sbx, sbz = math.huge, start_x, start_z
    for dz = -1, 1 do
        for dx = -1, 1 do
            local sx, sz = start_x + dx, start_z + dz
            local px = (sx + 0.15 + hash_unit(sx, sz, 0x100) * 0.7) * span
            local pz = (sz + 0.15 + hash_unit(sx, sz, 0x101) * 0.7) * span
            local d = (0.5 - px) ^ 2 + (0.5 - pz) ^ 2
            if d < sbest then
                sbest, sbx, sbz = d, sx, sz
            end
        end
    end
    if bx == sbx and bz == sbz then
        program = P.backrooms
    end

    program_cache[key] = program
    return program
end

local function edge_spec(a, b)
    if a == b then
        return a.edge
    end
    return { width = 1.8, height = 2.2, door = true }
end

-- builds the four border half walls and returns the port interval of each edge
local function perimeter(ctx, mat, h, opts)
    opts = opts or {}
    local ports = {}
    for _, side in ipairs({ "W", "E", "S", "N" }) do
        local e = ctx.edges[side]
        local axis = (side == "W" or side == "E") and "z" or "x"
        local low = side == "W" or side == "S"
        local line = low and WALL * 0.5 or CHUNK - WALL * 0.5
        local face = low and WALL or CHUNK - WALL
        local dir = low and 1 or -1
        if e.open then
            ports[side] = { { a = -1, b = CHUNK + 1 } }
        else
            local top = math.min(e.height, h + 0.05)
            local ops = { { a = HALF - e.width * 0.5, b = HALF + e.width * 0.5, top = top } }
            ports[side] = ops
            wall(ctx, mat, axis, line, WALL, 0, CHUNK, 0, h + 0.05, ops, "border_wall")
            if opts.skirting then
                skirting(ctx, opts.skirting, axis, face, dir, WALL, CHUNK - WALL, ops, opts.skirting_h)
            end
            if e.door then
                local a, b = ops[1].a, ops[1].b
                casing(ctx, "trim_door", axis, face, dir, a, b, top, 0.08, 0.025)
                -- the lower ordered program hangs the fire doors, held open against its side
                if ctx.program.order < e.other.order then
                    local w = (b - a) * 0.5
                    door_leaf(ctx, "door_fire", axis, face, dir, a, 1, w, top - 0.02, 0.05, 94)
                    door_leaf(ctx, "door_fire", axis, face, dir, b, -1, w, top - 0.02, 0.05, 94)
                end
                if ctx.program.exit_signs and top + 0.32 < h then
                    face_box(ctx, "exit_green", axis, face, dir, HALF - 0.2, HALF + 0.2, top + 0.12, top + 0.27, 0.05, false, "exit_sign")
                end
                -- a threshold strip hides the seam where two floors meet
                if axis == "x" then
                    aabb(ctx, "steel", a, 0, low and 0 or CHUNK - WALL, b, 0.008, low and WALL or CHUNK, false, "threshold")
                else
                    aabb(ctx, "steel", low and 0 or CHUNK - WALL, 0, a, low and WALL or CHUNK, 0.008, b, false, "threshold")
                end
            end
        end
    end
    return ports
end

--------------------------------------------------------------------------------
-- backrooms, irregular rooms from a recursive split, the familiar yellow
--------------------------------------------------------------------------------

local function build_backrooms(ctx)
    local h = P.backrooms.ceiling
    local rnd = ctx.rnd
    floor_slab(ctx, "br_carpet")
    ceiling_slab(ctx, "br_ceiling", h)
    local ports = perimeter(ctx, "br_wallpaper", h)

    local rooms = {}

    local function blocked(p, bound)
        for _, o in ipairs(bound) do
            if p > o.a - 0.5 and p < o.b + 0.5 then
                return true
            end
        end
        return false
    end

    local function make_openings(a0, a1)
        local len = a1 - a0
        local count = (len > 8 and rnd() < 0.45) and 2 or 1
        local list = {}
        for _ = 1, count do
            local r = rnd()
            local w, top
            if r < 0.5 then
                w, top = 1.0 + rnd() * 0.5, 2.15
            elseif r < 0.82 then
                w, top = 1.8 + rnd() * 1.4, 2.3
            else
                w, top = 3.0 + rnd() * 3.0, h + 1
            end
            w = math.min(w, len - 1.2)
            if w > 0.8 then
                for _ = 1, 6 do
                    local a = a0 + 0.6 + rnd() * (len - w - 1.2)
                    local ok = true
                    for _, o in ipairs(list) do
                        if a < o.b + 0.8 and a + w > o.a - 0.8 then
                            ok = false
                        end
                    end
                    if ok then
                        list[#list + 1] = { a = a, b = a + w, top = top }
                        break
                    end
                end
            end
        end
        return list
    end

    local function split(x0, z0, x1, z1, b, depth)
        local w, d = x1 - x0, z1 - z0
        local min_room = 3.4
        local can_x = w >= min_room * 2
        local can_z = d >= min_room * 2
        local stop = depth >= 7 or (not can_x and not can_z) or (w * d < 80 and rnd() < 0.3)
        if not stop then
            local vertical
            if can_x and can_z then
                vertical = (w > d * 1.25) or (not (d > w * 1.25) and rnd() < 0.5)
            else
                vertical = can_x
            end
            for _ = 1, 8 do
                local lo, hi = vertical and x0 or z0, vertical and x1 or z1
                local p = lo + min_room + rnd() * (hi - lo - 2 * min_room)
                p = math.floor(p * 2 + 0.5) * 0.5
                local ok
                if vertical then
                    ok = not blocked(p, b.S) and not blocked(p, b.N)
                else
                    ok = not blocked(p, b.W) and not blocked(p, b.E)
                end
                if ok then
                    local wall_h = rnd() < 0.08 and 1.15 or h + 0.05
                    if vertical then
                        local ops = make_openings(z0, z1)
                        ops.low = wall_h < h
                        wall(ctx, "br_wallpaper", "z", p, 0.2, z0 - 0.1, z1 + 0.1, 0, wall_h, ops)
                        split(x0, z0, p, z1, { W = b.W, E = ops, S = b.S, N = b.N }, depth + 1)
                        split(p, z0, x1, z1, { W = ops, E = b.E, S = b.S, N = b.N }, depth + 1)
                    else
                        local ops = make_openings(x0, x1)
                        ops.low = wall_h < h
                        wall(ctx, "br_wallpaper", "x", p, 0.2, x0 - 0.1, x1 + 0.1, 0, wall_h, ops)
                        split(x0, z0, x1, p, { W = b.W, E = b.E, S = b.S, N = ops }, depth + 1)
                        split(x0, p, x1, z1, { W = b.W, E = b.E, S = ops, N = b.N }, depth + 1)
                    end
                    return
                end
            end
        end
        rooms[#rooms + 1] = { x0 = x0, z0 = z0, x1 = x1, z1 = z1, sides = b }
    end

    split(WALL, WALL, CHUNK - WALL, CHUNK - WALL, { W = ports.W, E = ports.E, S = ports.S, N = ports.N }, 0)

    local function room_at(x, z, margin)
        for _, r in ipairs(rooms) do
            if x > r.x0 + margin and x < r.x1 - margin and z > r.z0 + margin and z < r.z1 - margin then
                return r
            end
        end
        return nil
    end

    -- a spot on a full height wall with no opening under the fitting, half its width plus the door casing clear
    local function solid_spot(list, a0, a1, half)
        if list.low then
            return nil
        end
        for _ = 1, 6 do
            local p = a0 + rnd() * math.max(0.1, a1 - a0)
            local clear = true
            for _, o in ipairs(list) do
                if p + half > o.a - 0.15 and p - half < o.b + 0.15 then
                    clear = false
                end
            end
            if clear then
                return p
            end
        end
        return nil
    end

    -- one continuous ceiling grid, fixtures fall where the rooms allow them
    local rotated = hash_unit(ctx.cx, ctx.cz, 0x3c) < 0.5
    for i = 0, 9 do
        for j = 0, 9 do
            local x, z = 2.4 + i * 4.8, 2.4 + j * 4.8
            if room_at(x, z, 0.9) then
                local lx, lz = rotated and 0.3 or 0.6, rotated and 0.6 or 0.3
                aabb(ctx, "fixture_frame", x - lx - 0.04, h - 0.035, z - lz - 0.04, x + lx + 0.04, h, z + lz + 0.04, false, "troffer")
                make_light(ctx, aabb(ctx, "light_panel", x - lx, h - 0.04, z - lz, x + lx, h - 0.03, z + lz, false, "diffuser"), fixtures.backrooms, 0.3)
                if hash_unit(ctx.ox + x, ctx.oz + z, 0x718) < 0.06 then
                    aabb(ctx, "replacement_tile", x + 0.9, h - 0.012, z - 0.3, x + 1.5, h - 0.004, z + 0.3, false, "stained_tile")
                end
            end
        end
    end

    local start = ctx.cx == 0 and ctx.cz == 0
    for _, r in ipairs(rooms) do
        local w, d = r.x1 - r.x0, r.z1 - r.z0
        -- the classic pillar field, only in rooms large enough to wander through
        if w * d > 90 and rnd() < 0.35 then
            for px = math.ceil((r.x0 + 1.4) / 4.8) * 4.8, r.x1 - 1.4, 4.8 do
                for pz = math.ceil((r.z0 + 1.4) / 4.8) * 4.8, r.z1 - 1.4, 4.8 do
                    if not (start and px < 5 and pz < 5) then
                        aabb(ctx, "br_wallpaper", px - 0.28, 0, pz - 0.28, px + 0.28, h + 0.02, pz + 0.28, true, "pillar")
                    end
                end
            end
        end
        if rnd() < 0.1 then
            local pw, pd = 1.2 + rnd() * 2.0, 1.0 + rnd() * 1.6
            local px = r.x0 + 0.4 + rnd() * math.max(0.1, w - pw - 0.8)
            local pz = r.z0 + 0.4 + rnd() * math.max(0.1, d - pd - 0.8)
            aabb(ctx, "carpet_wet", px, 0.002, pz, px + pw, 0.006, pz + pd, false, "damp_carpet")
        end
        if rnd() < 0.07 then
            local x = r.x0 + 1.0 + rnd() * math.max(0.1, w - 2.0)
            local z = r.z0 + 1.0 + rnd() * math.max(0.1, d - 2.0)
            if not (start and x < 5 and z < 5) then
                clone(ctx, "chair", x, z, rnd() * 360)
            end
        end
        if rnd() < 0.3 then
            local face = r.x0 <= WALL + 0.01 and WALL or r.x0 + 0.1
            local z = solid_spot(r.sides.W, r.z0 + 0.8, r.z1 - 0.8, 0.4)
            if z then
                face_box(ctx, "fixture_frame", "z", face, 1, z - 0.4, z + 0.4, 2.1, 2.36, 0.025, false, "vent")
                face_box(ctx, "vent_shadow", "z", face, 1, z - 0.36, z + 0.36, 2.12, 2.34, 0.028, false, "vent")
            end
        end
        if rnd() < 0.25 then
            local face = r.z0 <= WALL + 0.01 and WALL or r.z0 + 0.1
            local x = solid_spot(r.sides.S, r.x0 + 0.8, r.x1 - 0.8, 0.05)
            if x then
                face_box(ctx, "paint_white", "x", face, 1, x - 0.05, x + 0.05, 0.28, 0.4, 0.02, false, "outlet")
            end
        end
    end
end

--------------------------------------------------------------------------------
-- office, open plan floors with cubicles, meeting rooms and kitchenettes
--------------------------------------------------------------------------------

local function office_columns(ctx, h, skip)
    for _, x in ipairs({ 4, 12, 20 }) do
        for _, z in ipairs({ 4, 12, 20 }) do
            local blocked = false
            for _, r in ipairs(skip) do
                if x > r[1] - 0.5 and x < r[3] + 0.5 and z > r[2] - 0.5 and z < r[4] + 0.5 then
                    blocked = true
                end
            end
            if not blocked then
                aabb(ctx, "office_paint", x - 0.28, 0, z - 0.28, x + 0.28, h + 0.02, z + 0.28, true, "column")
                aabb(ctx, "skirting_dark", x - 0.3, 0, z - 0.3, x + 0.3, 0.1, z + 0.3, false, "column_base")
            end
        end
    end
end

local function office_cubicles(ctx, h, rnd)
    for _, z0 in ipairs({ 2.0, 8.6, 15.2 }) do
        local zm = z0 + 2.5
        local x0, count = 1.6, 7
        local x1 = x0 + count * 2.5
        aabb(ctx, "fabric_panel", x0 - 0.03, 0, zm - 0.03, x1 + 0.03, 1.4, zm + 0.03, true, "partition")
        for k = 0, count do
            local x = x0 + k * 2.5
            aabb(ctx, "fabric_panel", x - 0.03, 0, z0, x + 0.03, 1.4, z0 + 5.0, true, "partition")
        end
        for k = 0, count - 1 do
            local xa, xb = x0 + k * 2.5 + 0.05, x0 + (k + 1) * 2.5 - 0.05
            for side = -1, 1, 2 do
                local zi, zo = zm + side * 0.04, zm + side * 0.78
                aabb(ctx, "desk_top", xa, 0.72, zi, xb, 0.75, zo, false, "desk")
                aabb(ctx, "cabinet", xb - 0.45, 0, zi, xb, 0.72, zo, true, "pedestal")
                if rnd() < 0.55 then
                    local xc = (xa + xb) * 0.5
                    aabb(ctx, "screen_dead", xc - 0.26, 0.9, zm + side * 0.3, xc + 0.26, 1.22, zm + side * 0.33, false, "monitor")
                    aabb(ctx, "black_plastic", xc - 0.05, 0.75, zm + side * 0.28, xc + 0.05, 0.9, zm + side * 0.35, false, "monitor_stand")
                end
                if rnd() < 0.3 then
                    clone(ctx, "chair", (xa + xb) * 0.5 + (rnd() - 0.5) * 0.6, zm + side * (1.35 + rnd() * 0.4), side > 0 and 180 + (rnd() - 0.5) * 60 or (rnd() - 0.5) * 60)
                end
            end
        end
    end
    return {}
end

local function office_meeting(ctx, h, rnd)
    local x0, z0, x1, z1 = 2.5, 8.0, 13.0, 17.0
    local door = { a = 10.9, b = 11.9 }
    local function glass_run(axis, line, a0, a1, gap)
        local ops = gap and { gap } or {}
        for _, s in ipairs(free_spans(a0, a1, ops, true)) do
            if axis == "x" then
                aabb(ctx, "glass_clear", s[1], 0, line - 0.015, s[2], h, line + 0.015, true, "glass")
                aabb(ctx, "glass_frost", s[1], 1.4, line - 0.018, s[2], 1.52, line + 0.018, false, "manifestation")
            else
                aabb(ctx, "glass_clear", line - 0.015, 0, s[1], line + 0.015, h, s[2], true, "glass")
                aabb(ctx, "glass_frost", line - 0.018, 1.4, s[1], line + 0.018, 1.52, s[2], false, "manifestation")
            end
        end
        for m = a0, a1 + 0.01, 1.5 do
            local open = gap and m > gap.a - 0.05 and m < gap.b + 0.05
            if not open then
                if axis == "x" then
                    aabb(ctx, "steel", m - 0.025, 0, line - 0.035, m + 0.025, h, line + 0.035, false, "mullion")
                else
                    aabb(ctx, "steel", line - 0.035, 0, m - 0.025, line + 0.035, h, m + 0.025, false, "mullion")
                end
            end
        end
    end
    glass_run("x", z0, x0, x1)
    glass_run("x", z1, x0, x1, door)
    glass_run("z", x0, z0, z1)
    glass_run("z", x1, z0, z1)
    aabb(ctx, "fabric_panel", x0 + 0.05, 0.001, z0 + 0.05, x1 - 0.05, 0.005, z1 - 0.05, false, "meeting_carpet")
    local cz = (z0 + z1) * 0.5
    clone(ctx, "table", 6.7, cz, 90)
    clone(ctx, "table", 8.8, cz, 90)
    for i = 0, 2 do
        local x = 5.9 + i * 1.4
        if rnd() < 0.85 then clone(ctx, "chair", x, cz - 1.05, 0) end
        if rnd() < 0.85 then clone(ctx, "chair", x, cz + 1.05, 180) end
    end
    aabb(ctx, "whiteboard", x0 + 0.08, 0.9, cz - 1.0, x0 + 0.11, 2.1, cz + 1.0, false, "whiteboard")
    aabb(ctx, "steel", x0 + 0.05, 0.86, cz - 1.05, x0 + 0.14, 0.9, cz + 1.05, false, "whiteboard_tray")
    return { { x0 - 0.4, z0 - 0.4, x1 + 0.4, z1 + 0.4 } }
end

local function office_kitchen(ctx, h, rnd)
    local z = WALL
    aabb(ctx, "cabinet", 6, 0, z, 14, 0.88, z + 0.62, true, "base_cabinets")
    aabb(ctx, "counter", 5.95, 0.88, z, 14.05, 0.92, z + 0.66, false, "counter")
    aabb(ctx, "cabinet", 6, 1.45, z, 14, 2.15, z + 0.36, false, "wall_cabinets")
    aabb(ctx, "steel", 9.5, 0.9, z + 0.12, 10.3, 0.93, z + 0.5, false, "sink")
    aabb(ctx, "appliance_white", 14.1, 0, z, 14.95, 1.85, z + 0.72, true, "fridge")
    aabb(ctx, "vending_body", 2.0, 0, z + 0.1, 3.0, 1.9, z + 0.9, true, "vending_machine")
    make_light(ctx, aabb(ctx, "vending_glow", 2.08, 0.75, z + 0.9, 2.92, 1.8, z + 0.92, false, "vending_front"), fixtures.vending, -0.4)
    aabb(ctx, "appliance_white", 3.5, 0, z + 0.2, 3.85, 1.0, z + 0.55, true, "water_cooler")
    cyl(ctx, "water_bottle", 3.675, 1.0, z + 0.375, 0.14, 0.42, false, "bottle")
    for i = 0, 2 do
        local x, zz = 5.5 + i * 3.2, 5.0 + (i % 2) * 1.2
        cyl(ctx, "counter", x, 0.72, zz, 0.45, 0.03, false, "table_top")
        cyl(ctx, "steel", x, 0, zz, 0.04, 0.72, true, "table_stem")
        cyl(ctx, "steel", x, 0, zz, 0.28, 0.02, false, "table_base")
        if rnd() < 0.7 then clone(ctx, "chair", x - 0.85, zz, 90) end
        if rnd() < 0.5 then clone(ctx, "chair", x + 0.85, zz, 270) end
    end
    return { { 1.5, WALL, 15.5, 1.2 } }
end

local function office_empty(ctx, h, rnd)
    for _ = 1, 3 do
        if rnd() < 0.7 then
            clone(ctx, "chair", 2 + rnd() * 18, 2 + rnd() * 18, rnd() * 360)
        end
    end
    local bx, bz = 3 + rnd() * 14, 3 + rnd() * 14
    local stack = 1 + math.floor(rnd() * 3)
    for k = 0, stack - 1 do
        local s = 0.55 - k * 0.06
        rbox(ctx, "cardboard", bx + (rnd() - 0.5) * 0.1, 0.2 + k * 0.4, bz, s, 0.4, s * 0.8, rnd() * 20, k == 0, "box")
    end
    if rnd() < 0.6 then
        aabb(ctx, "screen_dead", bx + 1.2, 0, bz + 0.4, bx + 1.25, 0.34, bz + 0.95, false, "monitor_on_floor")
    end
    cyl(ctx, "planter", 1.2, 0, 20.8, 0.28, 0.5, true, "pot")
    sphere(ctx, "foliage", 1.2, 0.95, 20.8, 0.45, "plant")
    return {}
end

local function build_office(ctx)
    local h = P.office.ceiling
    floor_slab(ctx, "office_carpet")
    ceiling_slab(ctx, "ceiling_white", h)
    perimeter(ctx, "office_paint", h, { skirting = "skirting_dark" })

    local exclusions = {}
    local cubicle_quadrants = 0
    for q = 1, 4 do
        set_quadrant(ctx, q)
        local rnd = prng_new(hash_u32(ctx.cx, ctx.cz, 0x410 + q))
        local roll = rnd()
        local skip = {}
        if roll < 0.45 and cubicle_quadrants < 2 then
            cubicle_quadrants = cubicle_quadrants + 1
            office_cubicles(ctx, h, rnd)
            skip = { { 0, 0, 22, 22 } }
        elseif roll < 0.68 then
            skip = office_meeting(ctx, h, rnd)
            for _, r in ipairs(skip) do
                local ax0, ax1 = mapx(ctx, r[1]), mapx(ctx, r[3])
                local az0, az1 = mapz(ctx, r[2]), mapz(ctx, r[4])
                exclusions[#exclusions + 1] = { math.min(ax0, ax1), math.min(az0, az1), math.max(ax0, ax1), math.max(az0, az1) }
            end
        elseif roll < 0.85 then
            skip = office_kitchen(ctx, h, rnd)
        else
            skip = office_empty(ctx, h, rnd)
        end
        office_columns(ctx, h, skip)
    end
    set_quadrant(ctx, 0)

    -- the troffer grid ignores the layout, like a real suspended ceiling, except over glass walls
    for i = 0, 19 do
        for j = 0, 12 do
            local x, z = 1.2 + i * 2.4, 1.8 + j * 3.6
            local clear = true
            for _, r in ipairs(exclusions) do
                local on_edge = x > r[1] and x < r[3] and z > r[2] and z < r[4]
                    and (x < r[1] + 0.9 or x > r[3] - 0.9 or z < r[2] + 0.9 or z > r[4] - 0.9)
                if on_edge then clear = false end
            end
            if clear and (i + j) % 2 == 0 then
                aabb(ctx, "fixture_frame", x - 0.34, h - 0.03, z - 0.64, x + 0.34, h, z + 0.64, false, "troffer")
                make_light(ctx, aabb(ctx, "light_panel", x - 0.3, h - 0.035, z - 0.6, x + 0.3, h - 0.028, z + 0.6, false, "diffuser"), fixtures.troffer, 0.3)
            end
        end
    end
end

--------------------------------------------------------------------------------
-- hotel, a cross of narrow corridors lined with numbered doors
--------------------------------------------------------------------------------

-- room local frame, u runs along the corridor, v runs from the room side face toward the back
local function room_frame(axis, face)
    if axis == "x" then
        return function(u, v) return u, face - v end
    end
    return function(u, v) return face - v, u end
end

local function uv_box(ctx, f, mat, u0, v0, u1, v1, y0, y1, physics, label)
    local xa, za = f(u0, v0)
    local xb, zb = f(u1, v1)
    return aabb(ctx, mat, xa, y0, za, xb, y1, zb, physics, label)
end

local function uv_cyl(ctx, f, mat, u, v, y0, radius, height, physics, label)
    local x, z = f(u, v)
    return cyl(ctx, mat, x, y0, z, radius, height, physics, label)
end

local function hotel_room(ctx, h, axis, face, room, depth, rnd)
    local f = room_frame(axis, face)
    local u0, u1 = room.u0, room.u1
    local back = depth - 0.2
    local s = room.left and 1 or -1
    local bed_wall = room.left and u0 + 0.1 or u1 - 0.1
    local other_wall = room.left and u1 - 0.1 or u0 + 0.1

    uv_box(ctx, f, "hotel_wallpaper", u0 - 0.1, 0, u0 + 0.1, depth, 0, h + 0.05, true, "room_wall")
    uv_box(ctx, f, "hotel_wallpaper", u1 - 0.1, 0, u1 + 0.1, depth, 0, h + 0.05, true, "room_wall")

    -- window wall with an overcast nothing outside
    local uc = (u0 + u1) * 0.5
    local wa, wb = uc - 1.0, uc + 1.0
    uv_box(ctx, f, "hotel_wallpaper", u0, back, wa, depth, 0, h + 0.05, true, "window_wall")
    uv_box(ctx, f, "hotel_wallpaper", wb, back, u1, depth, 0, h + 0.05, true, "window_wall")
    uv_box(ctx, f, "hotel_wallpaper", wa, back, wb, depth, 0, 0.75, true, "window_wall")
    uv_box(ctx, f, "hotel_wallpaper", wa, back, wb, depth, 2.2, h + 0.05, true, "window_wall")
    make_light(ctx, uv_box(ctx, f, "window_overcast", wa, depth - 0.06, wb, depth - 0.02, 0.75, 2.2, false, "window_pane"), fixtures.window, 0.2)
    uv_box(ctx, f, "paint_white", uc - 0.025, back + 0.05, uc + 0.025, back + 0.1, 0.75, 2.2, false, "mullion")
    uv_box(ctx, f, "paint_white", wa, back - 0.02, wb, back + 0.1, 0.72, 0.77, false, "window_sill")
    uv_box(ctx, f, "curtain", wa - 0.35, back - 0.2, wa + 0.3, back - 0.06, 0.03, 2.35, false, "curtain")
    uv_box(ctx, f, "curtain", wb - 0.3, back - 0.2, wb + 0.35, back - 0.06, 0.03, 2.35, false, "curtain")
    uv_box(ctx, f, "brass", wa - 0.4, back - 0.16, wb + 0.4, back - 0.12, 2.36, 2.39, false, "curtain_rail")

    -- bathroom pod beside the entry, opposite the door
    local bu0, bu1 = room.left and u1 - 2.0 or u0 + 0.1, room.left and u1 - 0.1 or u0 + 2.0
    local bath_face = room.left and bu0 or bu1
    uv_box(ctx, f, "hotel_wallpaper", bu0, 2.2, bu1, 2.36, 0, h + 0.05, true, "bath_wall")
    uv_box(ctx, f, "hotel_wallpaper", bath_face - 0.08, 0, bath_face + 0.08, 0.3, 0, h + 0.05, true, "bath_wall")
    uv_box(ctx, f, "hotel_wallpaper", bath_face - 0.08, 1.1, bath_face + 0.08, 2.36, 0, h + 0.05, true, "bath_wall")
    uv_box(ctx, f, "hotel_wallpaper", bath_face - 0.08, 0.3, bath_face + 0.08, 1.1, 2.05, h + 0.05, true, "bath_wall")
    uv_box(ctx, f, "tile_white", bu0, 0, bu1, 2.2, 0.001, 0.01, false, "bath_floor")
    uv_box(ctx, f, "appliance_white", room.left and bu1 - 0.75 or bu0, 0.3, room.left and bu1 or bu0 + 0.75, 2.1, 0, 0.55, true, "bathtub")

    uv_box(ctx, f, "room_carpet", u0 + 0.1, 2.36, u1 - 0.1, back, 0.001, 0.006, false, "room_carpet")
    local pu0, pu1 = room.left and u0 + 0.1 or bu1 + 0.08, room.left and bu0 - 0.08 or u1 - 0.1
    uv_box(ctx, f, "room_carpet", pu0, 0, pu1, 2.36, 0.001, 0.006, false, "room_carpet")

    -- bed against the side wall shared with the neighbour
    local function span(a, b)
        return math.min(bed_wall + s * a, bed_wall + s * b), math.max(bed_wall + s * a, bed_wall + s * b)
    end
    local a, b = span(0, 0.08)
    uv_box(ctx, f, "wood_dark", a, 3.0, b, 5.0, 0, 1.15, true, "headboard")
    a, b = span(0.08, 2.08)
    uv_box(ctx, f, "bed_base", a, 3.2, b, 4.8, 0, 0.4, true, "bed")
    uv_box(ctx, f, "linen", a + 0.02, 3.22, b - 0.02, 4.78, 0.4, 0.6, false, "mattress")
    a, b = span(0.75, 2.12)
    uv_box(ctx, f, "bed_cover", a, 3.16, b, 4.84, 0.44, 0.63, false, "bedspread")
    a, b = span(0.14, 0.56)
    uv_box(ctx, f, "linen", a, 3.32, b, 3.96, 0.6, 0.73, false, "pillow")
    uv_box(ctx, f, "linen", a, 4.04, b, 4.68, 0.6, 0.73, false, "pillow")
    a, b = span(0, 0.45)
    uv_box(ctx, f, "wood_dark", a, 2.5, b, 2.98, 0, 0.55, true, "nightstand")
    uv_box(ctx, f, "wood_dark", a, 5.02, b, 5.5, 0, 0.55, true, "nightstand")
    local lu = bed_wall + s * 0.22
    uv_cyl(ctx, f, "brass", lu, 2.74, 0.55, 0.05, 0.3, false, "lamp_base")
    make_light(ctx, uv_cyl(ctx, f, "shade_warm", lu, 2.74, 0.82, 0.15, 0.24, false, "lamp_shade"), fixtures.lamp, 0.05, 0x3d1)

    -- dresser and a dead television on the opposite wall
    local da, db = math.min(other_wall, other_wall - s * 0.5), math.max(other_wall, other_wall - s * 0.5)
    uv_box(ctx, f, "wood_dark", da, 3.5, db, 5.2, 0, 0.8, true, "dresser")
    local ta, tb = math.min(other_wall - s * 0.2, other_wall - s * 0.25), math.max(other_wall - s * 0.2, other_wall - s * 0.25)
    uv_box(ctx, f, "screen_dead", ta, 3.8, tb, 4.9, 0.95, 1.55, false, "television")
    if rnd() < 0.6 then
        local cx, cz = f(uc + s * 0.4, back - 0.85)
        clone(ctx, "chair", cx, cz, rnd() * 360)
    end
    -- the one detail that says somebody was here
    if rnd() < 0.5 then
        a, b = span(1.2, 1.55)
        uv_box(ctx, f, "cardboard", a, 3.7, b, 4.15, 0.63, 0.83, false, "suitcase")
    end
end

local function hotel_lobby(ctx, h, axis, face, room, depth)
    local f = room_frame(axis, face)
    local u0, u1 = room.u0, room.u1
    uv_box(ctx, f, "hotel_wallpaper", u0 - 0.1, 0, u0 + 0.1, depth, 0, h + 0.05, true, "lobby_wall")
    uv_box(ctx, f, "hotel_wallpaper", u0, depth - 0.2, u1, depth, 0, h + 0.05, true, "lobby_wall")
    uv_box(ctx, f, "tile_floor", u0 + 0.1, 0, u1, depth - 0.2, 0.001, 0.008, false, "lobby_floor")
    local back = depth - 0.2
    local uc = (u0 + u1) * 0.5
    for k = -1, 1, 2 do
        local c = uc + k * 1.1
        uv_box(ctx, f, "elevator_metal", c - 0.5, back - 0.03, c + 0.5, back, 0, 2.1, false, "elevator_door")
        uv_box(ctx, f, "black_plastic", c - 0.005, back - 0.035, c + 0.005, back, 0, 2.1, false, "elevator_seam")
        uv_box(ctx, f, "chrome", c - 0.6, back - 0.05, c - 0.5, back, 0, 2.2, false, "elevator_frame")
        uv_box(ctx, f, "chrome", c + 0.5, back - 0.05, c + 0.6, back, 0, 2.2, false, "elevator_frame")
        uv_box(ctx, f, "chrome", c - 0.6, back - 0.05, c + 0.6, back, 2.1, 2.2, false, "elevator_frame")
        uv_box(ctx, f, "indicator_amber", c - 0.12, back - 0.02, c + 0.12, back, 2.28, 2.36, false, "floor_indicator")
    end
    uv_box(ctx, f, "brass", uc - 0.06, back - 0.02, uc + 0.06, back, 1.0, 1.3, false, "call_panel")
    uv_box(ctx, f, "button_glow", uc - 0.02, back - 0.03, uc + 0.02, back - 0.02, 1.12, 1.16, false, "call_button")
    uv_box(ctx, f, "wood_dark", u0 + 0.1, 2.2, u0 + 0.5, 3.8, 0, 0.8, true, "console")
    uv_box(ctx, f, "mirror", u0 + 0.1, 2.35, u0 + 0.12, 3.65, 1.1, 2.1, false, "mirror")
    uv_cyl(ctx, f, "linen", u0 + 0.3, 3.0, 0.8, 0.08, 0.3, false, "vase")
    make_light(ctx, uv_cyl(ctx, f, "dome_warm", uc, depth * 0.5, h - 0.06, 0.2, 0.06, false, "dome"), fixtures.dome, 0.2)
end

local function hotel_quadrant(ctx, h, c0, lobby)
    local depth = 7.0
    local t = 0.2
    local back = c0 - depth
    local rows =
    {
        { axis = "x", a0 = WALL, a1 = back },
        { axis = "z", a0 = WALL, a1 = c0 },
    }
    for ri, row in ipairs(rows) do
        local rnd = prng_new(hash_u32(ctx.cx * 4 + ctx.q, ctx.cz, 0x3b0 + ri))
        local len = row.a1 - row.a0
        local n = math.max(1, math.floor(len / 3.9))
        local rw = len / n
        local openings, rooms = {}, {}
        for i = 0, n - 1 do
            local u0 = row.a0 + i * rw
            local u1 = u0 + rw
            local left = i % 2 == 0
            local dc = left and (u0 + 0.95) or (u1 - 0.95)
            local room = { u0 = u0, u1 = u1, dc = dc, left = left }
            room.lobby = lobby and ri == 2 and i == n - 1
            room.open = not room.lobby and rnd() < 0.16
            if room.lobby then
                openings[#openings + 1] = { a = u0 + 0.5, b = u1 - 0.5, top = 2.4 }
            else
                openings[#openings + 1] = { a = dc - 0.46, b = dc + 0.46, top = 2.1 }
            end
            rooms[#rooms + 1] = room
        end
        wall(ctx, "hotel_wallpaper", row.axis, c0 - t * 0.5, t, 0, c0, 0, h + 0.05, openings, "corridor_wall")
        for _, s in ipairs(free_spans(0, c0, openings, true)) do
            face_box(ctx, "wood_panel", row.axis, c0, 1, s[1], s[2], 0, 0.92, 0.018, false, "wainscot")
            face_box(ctx, "wood_dark", row.axis, c0, 1, s[1], s[2], 0.92, 0.98, 0.035, false, "dado_rail")
        end
        for i, room in ipairs(rooms) do
            if room.lobby then
                casing(ctx, "wood_dark", row.axis, c0, 1, room.u0 + 0.5, room.u1 - 0.5, 2.4, 0.1, 0.03)
                hotel_lobby(ctx, h, row.axis, c0 - t, room, depth)
            else
                local a, b = room.dc - 0.46, room.dc + 0.46
                casing(ctx, "wood_dark", row.axis, c0, 1, a, b, 2.1, 0.08, 0.025)
                local hinge = room.left and a or b
                local toward = room.left and 1 or -1
                door_leaf(ctx, "hotel_door", row.axis, c0 - t, -1, hinge, toward, 0.92, 2.08, 0.045, room.open and 78 or 0)
                local latch = room.left and b - 0.1 or a + 0.1
                if not room.open then
                    face_box(ctx, "brass", row.axis, c0, 1, latch - 0.06, latch + 0.02, 0.98, 1.02, 0.06, false, "handle")
                end
                local plate = room.left and b + 0.16 or a - 0.3
                face_box(ctx, "brass", row.axis, c0, 1, plate, plate + 0.14, 1.5, 1.6, 0.008, false, "room_number")
                if room.open then
                    hotel_room(ctx, h, row.axis, c0 - t, room, depth, rnd)
                end
            end
            -- sconces pair up on the stretch between two doors that face away from each other
            if i % 2 == 1 and i < #rooms then
                local u = room.u1
                face_box(ctx, "brass", row.axis, c0, 1, u - 0.06, u + 0.06, 1.55, 1.85, 0.03, false, "sconce_plate")
                local x, z
                if row.axis == "x" then x, z = u, c0 + 0.13 else x, z = c0 + 0.13, u end
                make_light(ctx, cyl(ctx, "shade_warm", x, 1.6, z, 0.085, 0.24, false, "sconce_shade"), fixtures.sconce, 0.0)
            end
        end
    end
    -- the occasional trace of service
    local rnd = prng_new(hash_u32(ctx.cx * 4 + ctx.q, ctx.cz, 0x3c0))
    if rnd() < 0.25 then
        local u = 3 + rnd() * 10
        aabb(ctx, "steel", u, 0.02, c0 + 0.1, u + 0.45, 0.06, c0 + 0.45, false, "room_service_tray")
        cyl(ctx, "linen", u + 0.12, 0.06, c0 + 0.25, 0.08, 0.02, false, "plate")
    end
    if rnd() < 0.2 then
        local a = 5 + rnd() * 8
        face_box(ctx, "pipe_red", "z", c0, 1, a, a + 0.6, 0.75, 1.25, 0.2, true, "extinguisher_cabinet")
    end
end

local function build_hotel(ctx)
    local h = P.hotel.ceiling
    local c0 = HALF - 1.1
    floor_slab(ctx, "hotel_carpet")
    ceiling_slab(ctx, "hotel_ceiling", h)
    perimeter(ctx, "hotel_wallpaper", h, { skirting = "wood_dark" })
    local lobby_q = hash_u32(ctx.cx, ctx.cz, 0x3a1) % 4 + 1
    local has_lobby = hash_unit(ctx.cx, ctx.cz, 0x3a2) < 0.55
    for q = 1, 4 do
        set_quadrant(ctx, q)
        hotel_quadrant(ctx, h, c0, has_lobby and q == lobby_q)
    end
    set_quadrant(ctx, 0)
    for t = 2.25, CHUNK - 2, 4.5 do
        if math.abs(t - HALF) > 1.5 then
            make_light(ctx, cyl(ctx, "dome_warm", t, h - 0.06, HALF, 0.17, 0.06, false, "dome"), fixtures.dome, 0.2)
            make_light(ctx, cyl(ctx, "dome_warm", HALF, h - 0.06, t, 0.17, 0.06, false, "dome"), fixtures.dome, 0.2, 0x34)
        end
    end
    make_light(ctx, cyl(ctx, "dome_warm", HALF, h - 0.06, HALF, 0.22, 0.06, false, "dome"), fixtures.dome, 0.2)
    for k = -1, 1, 2 do
        aabb(ctx, "exit_green", HALF - 0.2, h - 0.32, HALF + k * 3.2 - 0.03, HALF + 0.2, h - 0.17, HALF + k * 3.2 + 0.03, false, "exit_sign")
        aabb(ctx, "steel", HALF - 0.005, h - 0.17, HALF + k * 3.2 - 0.005, HALF + 0.005, h, HALF + k * 3.2 + 0.005, false, "sign_rod")
    end
end

--------------------------------------------------------------------------------
-- school, locker lined corridors and classrooms seen through clerestory glass
--------------------------------------------------------------------------------

local function classroom(ctx, h, f, u0, u1, vmax, windows, rnd)
    local uc = (u0 + u1) * 0.5
    -- the chalkboard is on the front wall, students face it
    uv_box(ctx, f, "chalkboard", u0 + 1.8, 0, u1 - 1.8, 0.03, 0.9, 2.1, false, "chalkboard")
    uv_box(ctx, f, "wood_light", u0 + 1.75, 0, u1 - 1.75, 0.09, 0.86, 0.9, false, "chalk_tray")
    uv_box(ctx, f, "desk_top", uc + 1.2, 1.1, uc + 2.6, 1.8, 0, 0.76, true, "teacher_desk")
    -- the clock is a flat disc, its axis points out of the front wall
    local clock = uv_cyl(ctx, f, "paper", uc - 3.6, 0.03, 2.3, 0.16, 0.03, false, "clock")
    if f(0, 1) == f(0, 0) then
        clock.pitch = 90
    else
        clock.roll = 90
    end
    local stacked = rnd() < 0.35
    for row = 0, 2 do
        local v = 3.2 + row * 2.1
        if v < vmax - 1.2 then
            for col = -1, 1, 2 do
                local cu = uc + col * 1.6
                uv_box(ctx, f, "desk_top", cu - 0.65, v, cu + 0.65, v + 0.5, 0.7, 0.73, false, "desk")
                uv_box(ctx, f, "desk_frame", cu - 0.6, v + 0.45, cu + 0.6, v + 0.48, 0.1, 0.7, true, "desk_modesty")
                for seat = -1, 1, 2 do
                    local su = cu + seat * 0.32
                    if stacked then
                        uv_box(ctx, f, "chair_plastic", su - 0.2, v + 0.05, su + 0.2, v + 0.45, 0.73, 0.77, false, "chair_seat")
                        uv_box(ctx, f, "chair_plastic", su - 0.2, v + 0.42, su + 0.2, v + 0.45, 0.77, 1.15, false, "chair_back")
                    else
                        uv_box(ctx, f, "chair_plastic", su - 0.2, v + 0.62, su + 0.2, v + 1.02, 0.43, 0.47, false, "chair_seat")
                        uv_box(ctx, f, "chair_plastic", su - 0.2, v + 0.99, su + 0.2, v + 1.02, 0.47, 0.85, false, "chair_back")
                    end
                end
            end
        end
    end
    -- two rows of cool linear fixtures
    for k = 0, 2 do
        local v = 1.8 + k * 3.2
        if v < vmax - 0.8 then
            for col = -1, 1, 2 do
                local cu = uc + col * 2.2
                uv_box(ctx, f, "fixture_frame", cu - 0.7, v - 0.17, cu + 0.7, v + 0.17, h - 0.06, h, false, "fixture")
                make_light(ctx, uv_box(ctx, f, "panel_cool", cu - 0.65, v - 0.12, cu + 0.65, v + 0.12, h - 0.07, h - 0.06, false, "diffuser"), fixtures.school, 0.3)
            end
        end
    end
    -- tall windows onto a white nothing
    if windows then
        for k = 0, 2 do
            local v0 = 2.2 + k * 2.6
            if v0 + 2.0 < vmax then
                uv_box(ctx, f, "paint_white", u0, v0 - 0.06, u0 + 0.04, v0 + 2.06, 0.94, 2.56, false, "window_frame")
                make_light(ctx, uv_box(ctx, f, "window_overcast", u0 + 0.04, v0, u0 + 0.05, v0 + 2.0, 1.0, 2.5, false, "window_pane"), fixtures.window, 0.0)
                uv_box(ctx, f, "paint_white", u0 + 0.04, v0 + 0.97, u0 + 0.07, v0 + 1.03, 1.0, 2.5, false, "mullion")
                uv_box(ctx, f, "paint_white", u0 + 0.04, v0, u0 + 0.12, v0 + 2.0, 0.94, 1.0, false, "window_sill")
            end
        end
    end
end

local function build_school(ctx)
    local h = P.school.ceiling
    local c0 = HALF - 1.6
    local s = 11.3
    floor_slab(ctx, "lino")
    ceiling_slab(ctx, "ceiling_white", h)
    perimeter(ctx, "school_paint", h, { skirting = "skirting_dark" })

    for q = 1, 4 do
        set_quadrant(ctx, q)
        local rnd = prng_new(hash_u32(ctx.cx * 4 + q, ctx.cz, 0x510))
        -- corridor faces, doors and clerestory windows for rooms a, b (along z = c0) and a, c (along x = c0)
        local open_a, open_b, open_c = rnd() < 0.55, rnd() < 0.55, rnd() < 0.55
        local x_ops =
        {
            { a = 19.9, b = 20.9, top = 2.1, door = true, open = open_a },
            { a = 13.4, b = 18.0, top = 2.35, sill = 1.3, window = true },
            { a = 8.7,  b = 9.7,  top = 2.1, door = true, open = open_b },
            { a = 2.0,  b = 6.6,  top = 2.35, sill = 1.3, window = true },
        }
        local z_ops =
        {
            { a = 8.7,  b = 9.7,  top = 2.1, door = true, open = open_c },
            { a = 2.0,  b = 6.6,  top = 2.35, sill = 1.3, window = true },
            { a = 13.4, b = 18.0, top = 2.35, sill = 1.3, window = true },
        }
        for _, cfg in ipairs({ { "x", x_ops }, { "z", z_ops } }) do
            local axis, ops = cfg[1], cfg[2]
            wall(ctx, "school_paint", axis, c0 - 0.1, 0.2, 0, c0, 0, h + 0.05, ops, "corridor_wall")
            for _, o in ipairs(ops) do
                if o.window then
                    if axis == "x" then
                        aabb(ctx, "glass_clear", o.a, o.sill, c0 - 0.11, o.b, o.top, c0 - 0.09, true, "clerestory")
                    else
                        aabb(ctx, "glass_clear", c0 - 0.11, o.sill, o.a, c0 - 0.09, o.top, o.b, true, "clerestory")
                    end
                    casing(ctx, "steel", axis, c0, 1, o.a, o.b, o.top, 0.05, 0.02, o.sill)
                    casing(ctx, "steel", axis, c0 - 0.2, -1, o.a, o.b, o.top, 0.05, 0.02, o.sill)
                else
                    casing(ctx, "steel", axis, c0, 1, o.a, o.b, o.top, 0.06, 0.02)
                    door_leaf(ctx, "school_door", axis, c0 - 0.2, -1, o.a, 1, 1.0, 2.08, 0.045, o.open and 70 or 0)
                end
            end
            -- lockers wherever the wall is free, a notice board now and then instead
            for _, span in ipairs(free_spans(0.3, c0 - 0.3, ops, false)) do
                local a, b = span[1] + 0.12, span[2] - 0.12
                if b - a > 0.8 then
                    if rnd() < 0.25 then
                        face_box(ctx, "cork", axis, c0, 1, a + 0.2, math.min(b - 0.2, a + 2.4), 1.1, 2.0, 0.02, false, "notice_board")
                        for k = 0, 2 do
                            local pa = a + 0.35 + k * 0.6
                            if pa + 0.3 < b - 0.2 then
                                face_box(ctx, "paper", axis, c0 + 0.02, 1, pa, pa + 0.3, 1.35 + (k % 2) * 0.2, 1.75 + (k % 2) * 0.2, 0.003, false, "notice")
                            end
                        end
                    else
                        face_box(ctx, "lockers", axis, c0, 1, a, b, 0, 1.85, 0.45, true, "lockers")
                    end
                end
            end
            face_box(ctx, "skirting_dark", axis, c0, 1, 0.15, c0, 0, 0.1, 0.012, false, "skirting")
        end
        -- interior walls between the four rooms, d is a storeroom reached through b
        wall(ctx, "school_paint", "x", s, 0.2, 0, c0, 0, h + 0.05, { { a = 0.6, b = 1.5, top = 2.1 } }, "room_wall")
        wall(ctx, "school_paint", "z", s, 0.2, 0, c0, 0, h + 0.05, {}, "room_wall")
        door_leaf(ctx, "school_door", "x", s - 0.1, -1, 0.6, 1, 0.9, 2.08, 0.045, 25)

        -- room a: front wall z = s, room b: front wall z = s, room c: front wall x = s
        local fa = function(u, v) return u, s + 0.1 + v end
        local fc = function(u, v) return s + 0.1 + v, u end
        local west = canonical_edge(ctx, "W")
        local south = canonical_edge(ctx, "S")
        classroom(ctx, h, fa, s + 0.1, c0 - 0.2, c0 - s - 0.4, false, rnd)
        classroom(ctx, h, fa, WALL, s - 0.1, c0 - s - 0.4, west ~= nil, rnd)
        classroom(ctx, h, fc, WALL, s - 0.1, c0 - s - 0.4, south ~= nil, rnd)
        -- the storeroom
        for k = 0, 2 do
            local z = 1.5 + k * 3.0
            aabb(ctx, "steel", 1.2, 0, z, 9.5, 1.9, z + 0.5, true, "shelving")
            for n = 0, 4 do
                if rnd() < 0.6 then
                    local x = 1.5 + n * 1.7
                    aabb(ctx, "cardboard", x, 1.0 + (n % 2) * 0.5, z + 0.05, x + 0.6, 1.35 + (n % 2) * 0.5, z + 0.45, false, "box")
                end
            end
        end
        make_light(ctx, aabb(ctx, "panel_cool", 5, h - 0.05, 5, 6.2, h - 0.04, 5.3, false, "diffuser"), fixtures.school, 0.3, 0x5a0)
    end
    set_quadrant(ctx, 0)

    for t = 2, CHUNK - 2, 4 do
        if math.abs(t - HALF) > 1.8 then
            for k = 1, 2 do
                local x, z = t, HALF
                if k == 2 then x, z = HALF, t end
                local lx, lz = k == 1 and 0.7 or 0.12, k == 1 and 0.12 or 0.7
                aabb(ctx, "fixture_frame", x - lx - 0.03, h - 0.5, z - lz - 0.03, x + lx + 0.03, h - 0.44, z + lz + 0.03, false, "pendant")
                aabb(ctx, "steel", x - 0.01, h - 0.44, z - 0.01, x + 0.01, h, z + 0.01, false, "pendant_rod")
                make_light(ctx, aabb(ctx, "panel_cool", x - lx, h - 0.51, z - lz, x + lx, h - 0.5, z + lz, false, "diffuser"), fixtures.school, 0.1, 0x50 + k)
            end
        end
    end
    -- a colored border inlaid in the floor along every corridor edge
    for k = -1, 1, 2 do
        local e = HALF + k * 1.25
        aabb(ctx, "zone_blue", 0, 0.001, e - 0.08, CHUNK, 0.004, e + 0.08, false, "floor_border")
        aabb(ctx, "zone_blue", e - 0.08, 0.001, 0, e + 0.08, 0.004, CHUNK, false, "floor_border")
    end
end

--------------------------------------------------------------------------------
-- parking, a low concrete grid of columns, bays and buzzing tubes
--------------------------------------------------------------------------------

local function build_garage(ctx)
    local h = P.garage.ceiling
    local rnd = ctx.rnd
    floor_slab(ctx, "concrete_floor")
    local sodium = hash_unit(ctx.cx, ctx.cz, 0x610) < 0.4
    local zone_colors = { "zone_blue", "zone_green", "zone_red", "zone_violet", "paint_yellow" }
    local zone = zone_colors[hash_u32(math.floor(ctx.cx / 2), math.floor(ctx.cz / 2), 0x611) % #zone_colors + 1]

    -- a ramp to an upper level that is never lit
    local ramp = hash_unit(ctx.cx, ctx.cz, 0x612) < 0.3
    local holes = {}
    if ramp then
        holes[#holes + 1] = { 30.5, 33.0, 46.5, 40.5 }
    end
    slab_with_holes(ctx, "concrete", h, h + 0.3, 0, 0, CHUNK, CHUNK, holes, false)

    local ports = {}
    for _, side in ipairs({ "W", "E", "S", "N" }) do
        local e = ctx.edges[side]
        if not e.open then
            ports[side] = true
        end
    end
    perimeter(ctx, "concrete", h, {})
    -- painted band on walls that exist
    for side, _ in pairs(ports) do
        local axis = (side == "W" or side == "E") and "z" or "x"
        local low = side == "W" or side == "S"
        local face = low and WALL or CHUNK - WALL
        local dir = low and 1 or -1
        local e = ctx.edges[side]
        local ops = { { a = HALF - e.width * 0.5, b = HALF + e.width * 0.5, top = e.height } }
        for _, span in ipairs(free_spans(WALL, CHUNK - WALL, ops, true)) do
            face_box(ctx, "garage_paint", axis, face, dir, span[1], span[2], 0, 1.1, 0.008, false, "wall_band")
            face_box(ctx, zone, axis, face, dir, span[1], span[2], 1.1, 1.25, 0.008, false, "zone_stripe")
        end
    end

    -- columns and downstand beams on an 8 m grid
    local columns_z = { 16, 32 }
    for _, cz in ipairs(columns_z) do
        for _, cx in ipairs({ 4, 12, 20, 28, 36, 44 }) do
            aabb(ctx, "concrete", cx - 0.3, 0, cz - 0.3, cx + 0.3, h, cz + 0.3, true, "column")
            aabb(ctx, "hazard", cx - 0.31, 0, cz - 0.31, cx + 0.31, 0.7, cz + 0.31, false, "column_hazard")
            aabb(ctx, zone, cx - 0.31, 1.45, cz - 0.31, cx + 0.31, 1.8, cz + 0.31, false, "column_band")
            if (cx + cz) % 16 == 4 then
                aabb(ctx, "level_sign", cx - 0.2, 1.9, cz - 0.312, cx + 0.2, 2.2, cz - 0.305, false, "level_sign")
                aabb(ctx, "level_sign", cx - 0.2, 1.9, cz + 0.305, cx + 0.2, 2.2, cz + 0.312, false, "level_sign")
            end
        end
        aabb(ctx, "concrete", 0, h - 0.5, cz - 0.22, CHUNK, h, cz + 0.22, false, "beam")
    end
    -- beams and pipes stop at the ramp well so the ramp has headroom
    for _, bx in ipairs({ 4, 12, 20, 28, 36, 44 }) do
        if ramp and bx > 30.5 and bx < 46.5 then
            aabb(ctx, "concrete", bx - 0.2, h - 0.42, 0, bx + 0.2, h, 33.0, false, "beam")
            aabb(ctx, "concrete", bx - 0.2, h - 0.42, 40.5, bx + 0.2, h, CHUNK, false, "beam")
        else
            aabb(ctx, "concrete", bx - 0.2, h - 0.42, 0, bx + 0.2, h, CHUNK, false, "beam")
        end
    end
    aabb(ctx, "pipe_red", 0, h - 0.62, 10.0, CHUNK, h - 0.54, 10.08, false, "sprinkler_main")
    if ramp then
        aabb(ctx, "pipe_red", 0, h - 0.62, 38.0, 30.5, h - 0.54, 38.08, false, "sprinkler_main")
        aabb(ctx, "pipe_red", 46.5, h - 0.62, 38.0, CHUNK, h - 0.54, 38.08, false, "sprinkler_main")
    else
        aabb(ctx, "pipe_red", 0, h - 0.62, 38.0, CHUNK, h - 0.54, 38.08, false, "sprinkler_main")
    end

    -- bays either side of three east west aisles, one north south aisle crosses them
    local rows = { { 0.3, 4.8 }, { 11.2, 16.0 }, { 16.0, 20.8 }, { 27.2, 32.0 }, { 32.0, 36.8 }, { 43.2, 47.7 } }
    local function in_ramp(x, z)
        return ramp and x > 29.5 and x < 47.5 and z > 32 and z < 41.5
    end
    for ri, r in ipairs(rows) do
        local z0, z1 = r[1], r[2]
        for _, seg in ipairs({ { 0.6, 20.8 }, { 27.2, 47.4 } }) do
            local count = math.floor((seg[2] - seg[1]) / 2.5)
            for k = 0, count do
                local x = seg[1] + k * 2.5
                if not in_ramp(x, (z0 + z1) * 0.5) then
                    aabb(ctx, "paint_line", x - 0.05, 0.001, z0 + 0.15, x + 0.05, 0.004, z1 - 0.15, false, "bay_line")
                end
            end
            for k = 0, count - 1 do
                local x = seg[1] + k * 2.5 + 1.25
                local near_column = math.abs((x - 4) % 8) < 1.3 or math.abs((x - 4) % 8 - 8) < 1.3
                if not in_ramp(x, (z0 + z1) * 0.5) and rnd() < 0.35 then
                    local zs = (ri % 2 == 1) and z0 + 0.45 or z1 - 0.45
                    aabb(ctx, "paint_yellow", x - 0.8, 0, zs - 0.08, x + 0.8, 0.12, zs + 0.08, false, "wheel_stop")
                end
                if near_column and rnd() < 0.05 then
                    cyl(ctx, "cone_orange", x, 0, (z0 + z1) * 0.5, 0.17, 0.05, false, "cone_base")
                    push(ctx, { material = "cone_orange", mesh = MeshType.Cone, label = "cone", px = ctx.ox + x, py = 0.3, pz = ctx.oz + (z0 + z1) * 0.5, sx = 0.12, sy = 0.25, sz = 0.12 })
                end
            end
        end
    end

    -- tubes along every aisle, some chunks run on sodium
    local spec = sodium and fixtures.sodium or fixtures.batten
    local tube = sodium and "sodium_tube" or "tube_cool"
    for _, az in ipairs({ 8, 24, 40 }) do
        for x = 2.5, CHUNK - 2, 5 do
            if math.abs(x - HALF) > 2 and not (ramp and az == 40 and x > 29.5 and x < 47.5) then
                aabb(ctx, "fixture_frame", x - 0.64, h - 0.1, az - 0.07, x + 0.64, h - 0.04, az + 0.07, false, "batten")
                make_light(ctx, aabb(ctx, tube, x - 0.6, h - 0.13, az - 0.03, x + 0.6, h - 0.1, az + 0.03, false, "tube"), spec, 0.2)
            end
        end
    end
    for z = 2.5, CHUNK - 2, 5 do
        if math.abs(z - 8) > 2 and math.abs(z - 24) > 2 and math.abs(z - 40) > 2 then
            aabb(ctx, "fixture_frame", HALF - 0.07, h - 0.1, z - 0.64, HALF + 0.07, h - 0.04, z + 0.64, false, "batten")
            make_light(ctx, aabb(ctx, tube, HALF - 0.03, h - 0.13, z - 0.6, HALF + 0.03, h - 0.1, z + 0.6, false, "tube"), spec, 0.2, 0x62)
        end
    end

    if ramp then
        local rise = h + 0.3
        slope_x(ctx, "concrete_floor", 30.5, 46.5, 0.0, rise, 33.3, 40.2, 0.3, true, "ramp")
        aabb(ctx, "concrete", 30.5, 0, 32.9, 46.5, 1.0, 33.3, true, "ramp_wall")
        aabb(ctx, "concrete", 30.5, 0, 40.2, 46.5, 1.0, 40.6, true, "ramp_wall")
        -- the upper level is a shaft into darkness
        aabb(ctx, "concrete", 30.5, h + 0.3, 32.6, 47.5, h + 3.2, 33.0, false, "shaft")
        aabb(ctx, "concrete", 30.5, h + 0.3, 40.5, 47.5, h + 3.2, 40.9, false, "shaft")
        aabb(ctx, "concrete", 30.1, h + 0.3, 32.6, 30.5, h + 3.2, 40.9, false, "shaft")
        aabb(ctx, "concrete", 30.1, h + 3.2, 32.6, 47.5, h + 3.5, 40.9, false, "shaft")
        aabb(ctx, "concrete", 47.1, h + 0.3, 32.6, 47.5, h + 3.2, 40.9, true, "shaft")
        local barrier_y = rise * (44.5 - 30.5) / 16.0
        aabb(ctx, "hazard", 44.5, barrier_y, 33.3, 44.7, barrier_y + 1.0, 40.2, true, "barrier")
    end

    -- a stair core with a door that stays shut
    if hash_unit(ctx.cx, ctx.cz, 0x613) < 0.45 then
        local x0, z0, x1, z1 = 37.2, 11.6, 43.6, 20.4
        wall(ctx, "concrete", "x", z0, 0.25, x0, x1, 0, h, {}, "stair_core")
        wall(ctx, "concrete", "x", z1, 0.25, x0, x1, 0, h, {}, "stair_core")
        wall(ctx, "concrete", "z", x0, 0.25, z0, z1, 0, h, { { a = 15.4, b = 16.4, top = 2.1 } }, "stair_core")
        wall(ctx, "concrete", "z", x1, 0.25, z0, z1, 0, h, {}, "stair_core")
        door_leaf(ctx, "door_fire", "z", x0 - 0.125, -1, 15.4, 1, 1.0, 2.08, 0.05, 0)
        casing(ctx, "trim_door", "z", x0 - 0.125, -1, 15.4, 16.4, 2.1, 0.07, 0.02)
        face_box(ctx, "exit_green", "z", x0 - 0.125, -1, 15.7, 16.1, 2.2, 2.34, 0.04, false, "exit_sign")
        face_box(ctx, "pipe_red", "z", x0 - 0.125, -1, 17.2, 17.8, 0.9, 1.6, 0.18, true, "hose_cabinet")
    end
end

--------------------------------------------------------------------------------
-- mall, a double height concourse under skylights, shuttered shops and a gallery
--------------------------------------------------------------------------------

local shop_signs = { "sign_a", "sign_b", "sign_c", "sign_d", "sign_e" }

local function mall_unit(ctx, rnd, axis, face, a0, a1, depth, h_shop)
    -- storefront opening in the face wall, the unit behind is a box of its own
    local f = room_frame(axis, face)
    local kind = rnd()
    local lit = rnd() < 0.45
    uv_box(ctx, f, "mall_wall", a0, 0, a0 + 0.4, depth, 0, h_shop + 0.4, true, "pier")
    uv_box(ctx, f, "mall_wall", a1 - 0.4, 0, a1, depth, 0, h_shop + 0.4, true, "pier")
    uv_box(ctx, f, "mall_wall", a0 + 0.4, 0, a1 - 0.4, 0.3, 3.8, h_shop + 0.4, true, "lintel")
    local sign = uv_box(ctx, f, rnd() < 0.55 and shop_signs[math.floor(rnd() * #shop_signs) + 1] or "sign_dead", a0 + 0.9, -0.08, a1 - 0.9, 0, 3.95, 4.45, false, "shop_sign")
    uv_box(ctx, f, "mall_wall", a0, depth - 0.2, a1, depth, 0, h_shop, true, "unit_back")
    uv_box(ctx, f, "tile_floor", a0 + 0.4, 0.3, a1 - 0.4, depth - 0.2, 0.001, 0.008, false, "unit_floor")
    local fa, fb = a0 + 0.4, a1 - 0.4
    if kind < 0.4 then
        uv_box(ctx, f, "shutter", fa, 0.12, fb, 0.18, 0, 3.8, true, "shutter")
        uv_box(ctx, f, "steel", fa, 0.05, fb, 0.3, 3.6, 3.8, false, "shutter_hood")
        return
    end
    local door_a, door_b = (fa + fb) * 0.5 - 0.9, (fa + fb) * 0.5 + 0.9
    if kind < 0.8 then
        for _, span in ipairs({ { fa, door_a }, { door_b, fb } }) do
            uv_box(ctx, f, "glass_clear", span[1], 0.14, span[2], 0.16, 0, 3.8, true, "storefront")
            uv_box(ctx, f, "glass_frost", span[1], 0.135, span[2], 0.165, 1.4, 1.52, false, "manifestation")
            uv_box(ctx, f, "chrome", span[1], 0.1, span[2], 0.2, 0, 0.12, false, "storefront_base")
        end
        for _, m in ipairs({ fa, door_a, door_b, fb }) do
            uv_box(ctx, f, "chrome", m - 0.03, 0.1, m + 0.03, 0.2, 0, 3.8, false, "storefront_mullion")
        end
        uv_box(ctx, f, "chrome", fa, 0.1, fb, 0.2, 3.72, 3.8, false, "storefront_head")
    else
        uv_box(ctx, f, "shutter", fa, 0.12, fb, 0.18, 2.9, 3.8, false, "half_shutter")
    end
    -- the unit interior, rarely lit
    if lit then
        local uc = (a0 + a1) * 0.5
        make_light(ctx, uv_box(ctx, f, "panel_cool", uc - 0.6, depth * 0.5 - 0.3, uc + 0.6, depth * 0.5 + 0.3, h_shop - 0.02, h_shop, false, "shop_light"), fixtures.shop, 0.3)
    end
    local style = rnd()
    if style < 0.35 then
        for k = 0, 1 do
            local v = 2.5 + k * 2.6
            uv_box(ctx, f, "chrome", a0 + 1.2, v, a1 - 1.2, v + 0.04, 1.5, 1.54, false, "clothes_rail")
            for g = a0 + 1.4, a1 - 1.4, 0.35 do
                if rnd() < 0.7 then
                    local mats = { "garment_a", "garment_b", "garment_c" }
                    uv_box(ctx, f, mats[math.floor(rnd() * 3) + 1], g - 0.02, v - 0.25, g + 0.02, v + 0.29, 0.6, 1.5, false, "garment")
                end
            end
        end
        -- mannequins in the window, facing the concourse
        for m = 0, math.floor(rnd() * 3) do
            local u = a0 + 1.6 + m * 1.3
            if u < a1 - 1.2 then
                uv_cyl(ctx, f, "mannequin", u, 1.0, 0.0, 0.16, 0.02, false, "mannequin_base")
                uv_cyl(ctx, f, "mannequin", u, 1.0, 0.02, 0.02, 0.85, false, "mannequin_stand")
                uv_box(ctx, f, "mannequin", u - 0.18, 0.9, u + 0.18, 1.1, 0.85, 1.55, false, "mannequin_torso")
                local hx, hz = f(u, 1.0)
                sphere(ctx, "mannequin", hx, 1.7, hz, 0.12, "mannequin_head")
            end
        end
    elseif style < 0.7 then
        for k = 0, 2 do
            local v = 2.0 + k * 2.0
            if v < depth - 1.0 then
                uv_box(ctx, f, "steel", a0 + 1.0, v, a1 - 1.0, v + 0.45, 0, 1.6, true, "shelving")
            end
        end
    else
        uv_box(ctx, f, "counter", a0 + 1.0, depth - 1.8, a1 - 1.0, depth - 1.2, 0, 1.05, true, "counter")
        uv_box(ctx, f, lit and "menu_glow" or "sign_dead", a0 + 1.0, depth - 0.23, a1 - 1.0, depth - 0.2, 2.3, 3.3, false, "menu_board")
        for k = 0, 3 do
            local u = a0 + 1.4 + k * 1.2
            if u < a1 - 1.2 then
                uv_cyl(ctx, f, "chrome", u, depth - 2.3, 0, 0.18, 0.72, true, "stool")
            end
        end
    end
end

local function mall_quadrant(ctx, h_shop, h_roof, face_c, gallery_y, gap)
    local rnd = prng_new(hash_u32(ctx.cx * 4 + ctx.q, ctx.cz, 0x710))
    local depth = 8.0
    -- units along the z = face_c face and the x = face_c face
    for _, u in ipairs({ { WALL, 6.1 }, { 6.1, 12.1 }, { 12.1, face_c } }) do
        mall_unit(ctx, rnd, "x", face_c, u[1], u[2], depth, h_shop)
    end
    for _, u in ipairs({ { WALL, 5.1 }, { 5.1, face_c - depth } }) do
        mall_unit(ctx, rnd, "z", face_c, u[1], u[2], depth, h_shop)
    end
    -- the corner unit shows a blank flank to the other concourse
    aabb(ctx, "mall_wall", face_c - 0.3, 0, face_c - depth, face_c, h_shop + 0.4, face_c, true, "flank")
    aabb(ctx, "mall_ceiling", 0, h_shop, 0, face_c, h_shop + 0.2, face_c, false, "unit_ceiling")

    -- gallery on both faces, with a glass balustrade
    local g = face_c + 2.6
    aabb(ctx, "terrazzo", 0, gallery_y, face_c, g, gallery_y + 0.35, g, true, "gallery")
    aabb(ctx, "terrazzo", face_c, gallery_y, 0, g, gallery_y + 0.35, face_c, true, "gallery")
    local top = gallery_y + 0.35
    local runs = gap and { { 0, gap[1] }, { gap[2], g } } or { { 0, g } }
    for _, run in ipairs(runs) do
        aabb(ctx, "glass_clear", run[1], top, g - 0.03, run[2], top + 1.05, g, true, "balustrade")
        aabb(ctx, "chrome", run[1], top + 1.05, g - 0.05, run[2], top + 1.1, g + 0.02, false, "handrail")
    end
    aabb(ctx, "glass_clear", g - 0.03, top, 0, g, top + 1.05, g, true, "balustrade")
    aabb(ctx, "chrome", g - 0.05, top + 1.05, 0, g + 0.02, top + 1.1, g, false, "handrail")
    aabb(ctx, "mall_wall", 0, gallery_y - 0.4, g - 0.02, g + 0.02, gallery_y, g + 0.02, false, "gallery_fascia")
    aabb(ctx, "mall_wall", g - 0.02, gallery_y - 0.4, 0, g + 0.02, gallery_y, g, false, "gallery_fascia")
    -- the upper floor is dark glass with nothing behind it
    wall(ctx, "mall_wall", "x", face_c - 0.1, 0.2, 0, face_c, h_shop + 0.4, h_roof, {}, "upper_wall")
    wall(ctx, "mall_wall", "z", face_c - 0.1, 0.2, 0, face_c, h_shop + 0.4, h_roof, {}, "upper_wall")
    for _, cfg in ipairs({ { "x" }, { "z" } }) do
        local axis = cfg[1]
        for a = 1.0, face_c - 3.0, 6.0 do
            face_box(ctx, "glass_dark", axis, face_c, 1, a, a + 5.0, top, top + 3.2, 0.02, true, "upper_glass")
            face_box(ctx, rnd() < 0.3 and shop_signs[math.floor(rnd() * #shop_signs) + 1] or "sign_dead", axis, face_c, 1, a + 0.8, a + 4.2, top + 3.4, top + 3.9, 0.06, false, "upper_sign")
        end
    end
    -- downlights in the gallery soffit
    for a = 1.5, face_c + 1.5, 3.0 do
        make_light(ctx, cyl(ctx, "downlight", a, gallery_y - 0.02, face_c + 1.3, 0.09, 0.02, false, "downlight"), fixtures.downlight, 0.1)
        if a < face_c - 1 then
            make_light(ctx, cyl(ctx, "downlight", face_c + 1.3, gallery_y - 0.02, a, 0.09, 0.02, false, "downlight"), fixtures.downlight, 0.1, 0x72)
        end
    end
end

local function build_mall(ctx)
    local h_roof = P.mall.ceiling
    local h_shop = 4.6
    local gallery_y = 5.0
    local face_c = 18.0
    local rnd = ctx.rnd
    floor_slab(ctx, "terrazzo")
    perimeter(ctx, "mall_wall", h_roof, { skirting = "skirting_dark" })

    -- an escalator up to the gallery on one arm, frozen
    local esc_q = hash_unit(ctx.cx, ctx.cz, 0x75) < 0.5 and (hash_u32(ctx.cx, ctx.cz, 0x74) % 4) + 1 or nil
    local esc_x0, esc_steps, esc_run = 2.0, 27, 0.34
    local esc_x1 = esc_x0 + esc_steps * esc_run
    for q = 1, 4 do
        set_quadrant(ctx, q)
        mall_quadrant(ctx, h_shop, h_roof, face_c, gallery_y, q == esc_q and { esc_x1, esc_x1 + 1.2 } or nil)
    end
    set_quadrant(ctx, 0)

    -- roof with a cruciform skylight
    local holes = { { 0, 22, CHUNK, 26 }, { 22, 0, 26, CHUNK } }
    slab_with_holes(ctx, "mall_ceiling", h_roof, h_roof + 0.3, 0, 0, CHUNK, CHUNK, holes, false)
    aabb(ctx, "sky_panel", 0, h_roof + 0.25, 22, CHUNK, h_roof + 0.3, 26, false, "skylight")
    aabb(ctx, "sky_panel", 22, h_roof + 0.25, 0, 26, h_roof + 0.3, 22, false, "skylight")
    aabb(ctx, "sky_panel", 22, h_roof + 0.25, 26, 26, h_roof + 0.3, CHUNK, false, "skylight")
    for t = 1.5, CHUNK, 3.0 do
        aabb(ctx, "steel", t - 0.05, h_roof, 22, t + 0.05, h_roof + 0.25, 26, false, "skylight_mullion")
        aabb(ctx, "steel", 22, h_roof, t - 0.05, 26, h_roof + 0.25, t + 0.05, false, "skylight_mullion")
    end
    for t = 4, CHUNK - 3, 8 do
        make_light(ctx, aabb(ctx, "sky_panel", t - 0.01, h_roof + 0.2, HALF - 0.01, t + 0.01, h_roof + 0.21, HALF + 0.01, false, "sky_probe"), fixtures.skylight, 3.5)
        if math.abs(t - HALF) > 3 then
            make_light(ctx, aabb(ctx, "sky_panel", HALF - 0.01, h_roof + 0.2, t - 0.01, HALF + 0.01, h_roof + 0.21, t + 0.01, false, "sky_probe"), fixtures.skylight, 3.5, 0x73)
        end
    end

    -- a dry fountain in the court
    cyl(ctx, "terrazzo", HALF, 0, HALF, 3.2, 0.5, true, "fountain_rim")
    cyl(ctx, "tile_white", HALF, 0.5, HALF, 3.0, 0.01, false, "fountain_basin")
    cyl(ctx, "terrazzo", HALF, 0.5, HALF, 0.6, 0.9, true, "fountain_tier")
    cyl(ctx, "terrazzo", HALF, 1.4, HALF, 1.2, 0.12, false, "fountain_bowl")

    -- benches, planters and bins along the concourse
    for _, t in ipairs({ 6, 12, 36, 42 }) do
        for k = -1, 1, 2 do
            local x, z = t, HALF + k * 1.6
            aabb(ctx, "wood_light", x - 0.9, 0.42, z - 0.25, x + 0.9, 0.47, z + 0.25, false, "bench_seat")
            aabb(ctx, "steel", x - 0.8, 0, z - 0.2, x - 0.7, 0.42, z + 0.2, true, "bench_leg")
            aabb(ctx, "steel", x + 0.7, 0, z - 0.2, x + 0.8, 0.42, z + 0.2, true, "bench_leg")
            x, z = HALF + k * 3.2, t
            cyl(ctx, "planter", x, 0, z, 0.6, 0.55, true, "planter")
            sphere(ctx, "foliage", x, 1.0, z, 0.65, "shrub")
        end
        if rnd() < 0.5 then
            cyl(ctx, "steel", HALF + 1.1, 0, t + 1.1, 0.22, 0.8, true, "bin")
        end
    end

    if esc_q then
        set_quadrant(ctx, esc_q)
        local x0, x1, z0, z1 = esc_x0, esc_x1, face_c + 2.9, face_c + 3.9
        local rise = gallery_y + 0.35
        for k = 0, esc_steps - 1 do
            local y = (k + 1) * rise / esc_steps
            aabb(ctx, "metal_tread", x0 + k * esc_run, 0, z0, x0 + (k + 1) * esc_run, y, z1, true, "escalator_step")
        end
        aabb(ctx, "terrazzo", x1, gallery_y, face_c + 2.6, x1 + 1.2, rise, z1, true, "escalator_landing")
        slope_x(ctx, "glass_clear", x0, x1, 1.0, rise + 1.0, z0 - 0.04, z0, 1.0, true, "escalator_balustrade")
        slope_x(ctx, "glass_clear", x0, x1, 1.0, rise + 1.0, z1, z1 + 0.04, 1.0, true, "escalator_balustrade")
        slope_x(ctx, "black_plastic", x0, x1, 1.52, rise + 1.52, z0 - 0.06, z0 + 0.02, 0.06, false, "escalator_handrail")
        slope_x(ctx, "black_plastic", x0, x1, 1.52, rise + 1.52, z1 - 0.02, z1 + 0.06, 0.06, false, "escalator_handrail")
        set_quadrant(ctx, 0)
    end
end

--------------------------------------------------------------------------------
-- pools, a tiled natatorium or the flooded pool rooms, water sits a little below the deck
--------------------------------------------------------------------------------

-- a horizontal round bar between two points that differ along x or along z
local function bar(ctx, mat, x0, z0, x1, z1, y, radius, label)
    local job = push(ctx,
    {
        material = mat, label = label or "rail", mesh = MeshType.Cylinder,
        px = ctx.ox + mapx(ctx, (x0 + x1) * 0.5), py = y, pz = ctx.oz + mapz(ctx, (z0 + z1) * 0.5),
        sx = radius, sy = math.max(math.abs(x1 - x0), math.abs(z1 - z0)), sz = radius,
    })
    if math.abs(x1 - x0) > math.abs(z1 - z0) then
        job.roll = 90
    else
        job.pitch = 90
    end
    return job
end

-- a flat disc on a wall face, lamps and clocks, the axis points out of the wall
local function disc(ctx, mat, axis, face, dir, a, y, radius, depth, label)
    if axis == "x" then
        local job = cyl(ctx, mat, a, y, face + dir * depth * 0.5, radius, depth, false, label)
        job.py, job.pitch = y, 90
        return job
    end
    local job = cyl(ctx, mat, face + dir * depth * 0.5, y, a, radius, depth, false, label)
    job.py, job.roll = y, 90
    return job
end

-- a sunken basin, the deck slab must already leave the hole
local function basin(ctx, x0, z0, x1, z1, depth, water_y, opts)
    opts = opts or {}
    local tile = opts.tile or "pool_tile_aqua"
    local t = 0.3
    local yb = -depth - 0.3
    aabb(ctx, tile, x0, yb, z0, x1, -depth, z1, true, "basin_floor")
    aabb(ctx, tile, x0 - t, yb, z0 - t, x0, 0, z1 + t, true, "basin_wall")
    aabb(ctx, tile, x1, yb, z0 - t, x1 + t, 0, z1 + t, true, "basin_wall")
    aabb(ctx, tile, x0, yb, z0 - t, x1, 0, z0, true, "basin_wall")
    aabb(ctx, tile, x0, yb, z1, x1, 0, z1 + t, true, "basin_wall")
    -- the dark band every pool has where the water meets the tiles
    if opts.band then
        face_box(ctx, "pool_tile_band", "z", x0, 1, z0, z1, -0.24, 0, 0.004, false, "waterline")
        face_box(ctx, "pool_tile_band", "z", x1, -1, z0, z1, -0.24, 0, 0.004, false, "waterline")
        face_box(ctx, "pool_tile_band", "x", z0, 1, x0, x1, -0.24, 0, 0.004, false, "waterline")
        face_box(ctx, "pool_tile_band", "x", z1, -1, x0, x1, -0.24, 0, 0.004, false, "waterline")
    end
    if opts.coping then
        local c = 0.32
        aabb(ctx, "coping", x0 - c, 0, z0 - c, x1 + c, 0.03, z0 + 0.04, false, "coping")
        aabb(ctx, "coping", x0 - c, 0, z1 - 0.04, x1 + c, 0.03, z1 + c, false, "coping")
        aabb(ctx, "coping", x0 - c, 0, z0 + 0.04, x0 + 0.04, 0.03, z1 - 0.04, false, "coping")
        aabb(ctx, "coping", x1 - 0.04, 0, z0 + 0.04, x1 + c, 0.03, z1 - 0.04, false, "coping")
    end
    aabb(ctx, "pool_water", x0, water_y - 0.02, z0, x1, water_y, z1, false, "water")
end

-- a ladder hung on a basin wall, the rails rise out of the water and curl over onto the deck
-- axis and line name the wall face like wall(), dir points from the wall into the water
local function pool_ladder(ctx, axis, line, dir, a, reach)
    local function at(u, v)
        if axis == "x" then
            return u, v
        end
        return v, u
    end
    reach = reach or 1.15
    local v_water, v_deck = line + dir * 0.16, line - dir * 0.38
    for _, u in ipairs({ a - 0.26, a + 0.26 }) do
        local wx, wz = at(u, v_water)
        local dx, dz = at(u, v_deck)
        cyl(ctx, "chrome", wx, -reach, wz, 0.022, reach + 0.85, false, "ladder_rail")
        cyl(ctx, "chrome", dx, 0, dz, 0.022, 0.85, true, "ladder_rail")
        bar(ctx, "chrome", wx, wz, dx, dz, 0.85, 0.022, "ladder_rail")
        sphere(ctx, "chrome", wx, 0.85, wz, 0.032, "ladder_joint")
        sphere(ctx, "chrome", dx, 0.85, dz, 0.032, "ladder_joint")
        cyl(ctx, "steel", dx, 0, dz, 0.05, 0.035, false, "ladder_flange")
    end
    local va, vb = math.min(line, line + dir * 0.2), math.max(line, line + dir * 0.2)
    for k = 1, 3 do
        local y = -0.28 * k
        if axis == "x" then
            aabb(ctx, "steel", a - 0.25, y - 0.025, va, a + 0.25, y, vb, false, "ladder_tread")
        else
            aabb(ctx, "steel", va, y - 0.025, a - 0.25, vb, y, a + 0.25, false, "ladder_tread")
        end
    end
end

-- a railing of posts with a top and a mid bar, along x or along z
local function railing(ctx, mat, x0, z0, x1, z1, y0, height, spacing)
    local len = math.max(math.abs(x1 - x0), math.abs(z1 - z0))
    local n = math.max(1, math.floor(len / (spacing or 1.5) + 0.5))
    for k = 0, n do
        local t = k / n
        cyl(ctx, mat, x0 + (x1 - x0) * t, y0, z0 + (z1 - z0) * t, 0.02, height, true, "rail_post")
    end
    bar(ctx, mat, x0, z0, x1, z1, y0 + height, 0.025, "rail_top")
    bar(ctx, mat, x0, z0, x1, z1, y0 + height * 0.5, 0.015, "rail_mid")
end

local function pool_wall_band(ctx, ports, h)
    for side, ops in pairs(ports) do
        local axis = (side == "W" or side == "E") and "z" or "x"
        local low = side == "W" or side == "S"
        local face = low and WALL or CHUNK - WALL
        local dir = low and 1 or -1
        for _, span in ipairs(free_spans(WALL, CHUNK - WALL, ops, true)) do
            face_box(ctx, "pool_tile_band", axis, face, dir, span[1], span[2], 1.1, 1.28, 0.006, false, "wall_band")
        end
    end
end

local function build_natatorium(ctx)
    local h = 5.5
    local rnd = ctx.rnd
    local x0, x1 = 9.0, 34.0
    local lanes, lane_w = 6, 2.25
    local z0 = 13.0
    local z1 = z0 + lanes * lane_w
    local depth = 1.8
    local wy = -0.12

    slab_with_holes(ctx, "pool_deck", -0.3, 0, 0, 0, CHUNK, CHUNK, { { x0, z0, x1, z1 } }, true)
    local ports = perimeter(ctx, "pool_tile", h, { skirting = "pool_tile_band", skirting_h = 0.15 })
    pool_wall_band(ctx, ports, h)
    basin(ctx, x0, z0, x1, z1, depth, wy, { band = true, coping = true })

    -- lane lines and end wall targets, lanes read through the water from the deck
    for k = 0, lanes - 1 do
        local zc = z0 + (k + 0.5) * lane_w
        aabb(ctx, "lane_blue", x0 + 2.0, -depth, zc - 0.12, x1 - 2.0, -depth + 0.006, zc + 0.12, false, "lane_line")
        aabb(ctx, "lane_blue", x0 + 2.0, -depth, zc - 0.5, x0 + 2.25, -depth + 0.006, zc + 0.5, false, "lane_t")
        aabb(ctx, "lane_blue", x1 - 2.25, -depth, zc - 0.5, x1 - 2.0, -depth + 0.006, zc + 0.5, false, "lane_t")
        face_box(ctx, "lane_blue", "z", x1, -1, zc - 0.12, zc + 0.12, -depth, -0.35, 0.006, false, "target")
        face_box(ctx, "lane_blue", "z", x1, -1, zc - 0.4, zc + 0.4, -0.7, -0.5, 0.006, false, "target")
        if k > 0 then
            face_box(ctx, "lane_blue", "z", x0, 1, zc - 0.12, zc + 0.12, -depth, -0.35, 0.006, false, "target")
        end
    end

    -- floating lane ropes, red near the walls
    for k = 1, lanes - 1 do
        local z = z0 + k * lane_w
        local seg = 2.5
        local n = math.floor((x1 - x0) / seg + 0.5)
        -- the first rope clears the handrail of the walk in steps
        for s = (k == 1) and 1 or 0, n - 1 do
            local a = x0 + s * seg
            local mat = (s < 2 or s >= n - 2) and "rope_red" or (s % 2 == 0 and "rope_blue" or "rope_white")
            bar(ctx, mat, a, z, a + seg, z, wy + 0.01, 0.05, "lane_rope")
        end
    end

    -- backstroke flags five meters from each wall
    local flag_colors = { "rope_red", "rope_white", "rope_blue" }
    for _, fx in ipairs({ x0 + 5.0, x1 - 5.0 }) do
        local za, zb = z0 - 1.0, z1 + 1.0
        cyl(ctx, "chrome", fx, 0, za, 0.03, 2.0, true, "flag_post")
        cyl(ctx, "chrome", fx, 0, zb, 0.03, 2.0, true, "flag_post")
        bar(ctx, "rope_white", fx, za, fx, zb, 1.88, 0.008, "flag_line")
        local i = 0
        for z = za + 0.45, zb - 0.3, 0.5 do
            i = i + 1
            aabb(ctx, flag_colors[i % 3 + 1], fx - 0.003, 1.64, z - 0.1, fx + 0.003, 1.88, z + 0.1, false, "flag")
        end
    end

    -- starting blocks at the deep end
    for k = 0, lanes - 1 do
        local zc = z0 + (k + 0.5) * lane_w
        aabb(ctx, "lifeguard_white", x1 + 0.35, 0, zc - 0.28, x1 + 0.95, 0.68, zc + 0.28, true, "starting_block")
        aabb(ctx, "lane_blue", x1 + 0.3, 0.68, zc - 0.3, x1 + 1.0, 0.74, zc + 0.3, false, "block_top")
        bar(ctx, "chrome", x1 + 0.36, zc - 0.22, x1 + 0.36, zc + 0.22, 0.45, 0.018, "backstroke_grip")
    end

    pool_ladder(ctx, "x", z0, 1, x1 - 1.6)
    pool_ladder(ctx, "x", z1, -1, x1 - 1.6)

    -- walk in steps at the shallow end with a nosing stripe and a handrail
    local steps = 6
    local rise = depth / steps
    local sz0, sz1 = z0, z0 + lane_w
    for i = 1, steps - 1 do
        local xa, xb = x0 + 0.4 * (i - 1), x0 + 0.4 * i
        aabb(ctx, "pool_tile", xa, -depth, sz0, xb, -rise * i, sz1, true, "pool_step")
        aabb(ctx, "lane_blue", xb - 0.06, -rise * i, sz0, xb, -rise * i + 0.004, sz1, false, "step_nosing")
    end
    cyl(ctx, "chrome", x0 - 0.4, 0, sz1 + 0.05, 0.025, 0.9, true, "handrail_post")
    cyl(ctx, "chrome", x0 + 2.0, -depth + 1.2 * rise, sz1 + 0.05, 0.025, 1.0, false, "handrail_post")
    slope_x(ctx, "chrome", x0 - 0.4, x0 + 2.0, 0.9, -depth + 1.2 * rise + 1.0, sz1 + 0.025, sz1 + 0.075, 0.05, false, "handrail")

    -- underwater lights along the long walls
    for x = x0 + 2.5, x1 - 2.4, 5.0 do
        make_light(ctx, disc(ctx, "pool_light", "x", z0, 1, x, -0.85, 0.15, 0.03, "underwater_light"), fixtures.underwater, 0.0)
        make_light(ctx, disc(ctx, "pool_light", "x", z1, -1, x, -0.85, 0.15, 0.03, "underwater_light"), fixtures.underwater, 0.0, 0x81)
    end

    -- the lifeguard chair watches the lanes from the south deck
    local gx, gz = HALF - 2.5, z0 - 1.8
    for _, o in ipairs({ { -0.35, -0.3 }, { 0.35, -0.3 }, { -0.35, 0.3 }, { 0.35, 0.3 } }) do
        cyl(ctx, "lifeguard_white", gx + o[1], 0, gz + o[2], 0.04, 1.8, true, "lifeguard_leg")
    end
    aabb(ctx, "lifeguard_white", gx - 0.42, 1.8, gz - 0.36, gx + 0.42, 1.86, gz + 0.36, false, "lifeguard_seat")
    aabb(ctx, "lifeguard_white", gx - 0.42, 1.86, gz - 0.36, gx + 0.42, 2.5, gz - 0.31, false, "lifeguard_back")
    for k = 1, 4 do
        bar(ctx, "lifeguard_white", gx - 0.35, gz + 0.3, gx + 0.35, gz + 0.3, k * 0.4, 0.022, "lifeguard_rung")
    end
    aabb(ctx, "rope_red", gx + 0.43, 0.9, gz - 0.12, gx + 0.52, 1.7, gz + 0.12, false, "rescue_tube")

    -- bleachers along the north deck
    local zb = z1 + 3.0
    local bx0, bx1 = x0 + 1.0, x1 - 1.0
    for i = 0, 3 do
        local za = zb + i * 0.85
        local top = 0.45 * (i + 1)
        aabb(ctx, "concrete", bx0, 0, za, bx1, top, za + 0.85, true, "bleacher")
        aabb(ctx, "chair_plastic", bx0, top, za + 0.05, bx1, top + 0.04, za + 0.45, false, "bleacher_seat")
    end
    railing(ctx, "chrome", bx0, zb - 0.1, bx1, zb - 0.1, 0, 1.0, 2.0)
    if rnd() < 0.6 then
        local tx = bx0 + 1 + rnd() * (bx1 - bx0 - 2)
        aabb(ctx, rnd() < 0.5 and "garment_a" or "garment_b", tx - 0.35, 0.94, zb + 0.9, tx + 0.35, 0.96, zb + 1.3, false, "towel")
    end

    -- pace clock above the south deck
    disc(ctx, "black_plastic", "x", WALL, 1, HALF, 3.1, 0.66, 0.03, "clock_rim")
    disc(ctx, "lifeguard_white", "x", WALL + 0.03, 1, HALF, 3.1, 0.6, 0.02, "pace_clock")
    aabb(ctx, "black_plastic", HALF - 0.015, 3.1, WALL + 0.05, HALF + 0.015, 3.6, WALL + 0.06, false, "clock_hand")
    aabb(ctx, "rope_red", HALF, 3.09, WALL + 0.06, HALF + 0.45, 3.11, WALL + 0.07, false, "clock_hand")

    -- kickboards stacked by the wall, a bench, a drain line along the deck
    local kick = { "zone_blue", "paint_yellow", "zone_red", "zone_green" }
    for k = 0, 5 do
        aabb(ctx, kick[k % 4 + 1], 44.0, k * 0.035, 3.0, 44.45, k * 0.035 + 0.03, 3.3, false, "kickboard")
    end
    aabb(ctx, "wood_light", 36.0, 0.42, WALL + 0.05, 42.0, 0.47, WALL + 0.45, false, "bench_seat")
    aabb(ctx, "steel", 36.2, 0, WALL + 0.1, 36.3, 0.42, WALL + 0.4, true, "bench_leg")
    aabb(ctx, "steel", 41.7, 0, WALL + 0.1, 41.8, 0.42, WALL + 0.4, true, "bench_leg")
    aabb(ctx, "black_plastic", x0, 0, z0 - 0.6, x1, 0.004, z0 - 0.5, false, "deck_drain")
    aabb(ctx, "black_plastic", x0, 0, z1 + 0.5, x1, 0.004, z1 + 0.6, false, "deck_drain")

    -- roof trusses, then a skylight over the water or high bay pendants everywhere
    local skylight = rnd() < 0.5
    local holes = skylight and { { x0 + 1.0, z0 + 1.0, x1 - 1.0, z1 - 1.0 } } or {}
    slab_with_holes(ctx, "ceiling_white", h, h + 0.3, 0, 0, CHUNK, CHUNK, holes, false)
    for x = 4.0, CHUNK - 3.0, 5.0 do
        aabb(ctx, "lifeguard_white", x - 0.12, h - 0.7, WALL, x + 0.12, h, CHUNK - WALL, false, "truss")
        aabb(ctx, "lifeguard_white", x - 0.02, h - 0.68, WALL, x + 0.02, h - 0.02, CHUNK - WALL, false, "truss_web")
    end
    if skylight then
        aabb(ctx, "sky_panel", x0 + 1.0, h + 0.25, z0 + 1.0, x1 - 1.0, h + 0.3, z1 - 1.0, false, "skylight")
        for x = x0 + 3.5, x1 - 2.0, 6.0 do
            make_light(ctx, aabb(ctx, "sky_panel", x - 0.01, h + 0.2, HALF - 0.01, x + 0.01, h + 0.21, HALF + 0.01, false, "sky_probe"), fixtures.skylight, 3.0)
        end
    end
    for x = 6.5, CHUNK - 3.0, 5.0 do
        for _, z in ipairs({ 5.5, z0 + 3.5, z1 - 3.5, 35.5, 43.0 }) do
            if not (skylight and x > x0 and x < x1 and z > z0 and z < z1) then
                cyl(ctx, "steel", x, h - 1.4, z, 0.008, 1.4, false, "pendant_cable")
                cyl(ctx, "steel", x, h - 1.55, z, 0.36, 0.15, false, "pendant_body")
                make_light(ctx, cyl(ctx, "panel_cool", x, h - 1.56, z, 0.32, 0.01, false, "pendant_lens"), fixtures.hall, 0.2)
            end
        end
    end
end

-- the pool rooms, a warren of white tiled halls half under water
local function build_poolrooms(ctx)
    local h = 3.6
    local rnd = ctx.rnd
    local cell = 16.0
    local holes, cells = {}, {}
    for j = 0, 2 do
        for i = 0, 2 do
            local r = rnd()
            local kind = r < 0.45 and "flood" or (r < 0.65 and "deep" or (r < 0.87 and "dry" or "stairs"))
            local c = { i = i, j = j, kind = kind, x0 = i * cell, z0 = j * cell, x1 = (i + 1) * cell, z1 = (j + 1) * cell }
            if kind == "flood" then
                c.hole = { c.x0 + 1.4, c.z0 + 1.4, c.x1 - 1.4, c.z1 - 1.4 }
            elseif kind == "deep" then
                c.hole = { c.x0 + 3.0, c.z0 + 4.0, c.x1 - 3.0, c.z1 - 4.0 }
            end
            if c.hole then
                holes[#holes + 1] = c.hole
            end
            cells[#cells + 1] = c
        end
    end

    slab_with_holes(ctx, "pool_tile", -0.3, 0, 0, 0, CHUNK, CHUNK, holes, true)
    local ports = perimeter(ctx, "pool_tile", h, { skirting = "pool_tile_band", skirting_h = 0.12 })

    -- inner walls on the cell lines, each run keeps at least one arch so the warren stays connected
    for _, line in ipairs({ cell, cell * 2 }) do
        for _, axis in ipairs({ "x", "z" }) do
            for k = 0, 2 do
                local a0, a1 = k * cell, (k + 1) * cell
                if rnd() < 0.72 then
                    local ops = {}
                    local count = rnd() < 0.35 and 2 or 1
                    for n = 1, count do
                        local w = 2.4 + rnd() * 1.6
                        local c = a0 + (n - 0.5) * cell / count + (rnd() - 0.5) * 2.0
                        ops[#ops + 1] = { a = c - w * 0.5, b = c + w * 0.5, top = 2.7 }
                    end
                    wall(ctx, "pool_tile", axis, line, 0.3, a0 + (k == 0 and WALL or 0.15), a1 - (k == 2 and WALL or 0.15), 0, h, ops, "tiled_wall")
                    for _, o in ipairs(ops) do
                        casing(ctx, "pool_tile_band", axis, line + 0.15, 1, o.a, o.b, o.top, 0.12, 0.012)
                        casing(ctx, "pool_tile_band", axis, line - 0.15, -1, o.a, o.b, o.top, 0.12, 0.012)
                    end
                else
                    -- no wall, a pair of tiled piers marks the cell line instead
                    for _, a in ipairs({ a0 + 0.8, a1 - 0.8 }) do
                        if axis == "x" then
                            aabb(ctx, "pool_tile", a - 0.35, 0, line - 0.35, a + 0.35, h, line + 0.35, true, "pier")
                        else
                            aabb(ctx, "pool_tile", line - 0.35, 0, a - 0.35, line + 0.35, h, a + 0.35, true, "pier")
                        end
                    end
                end
            end
        end
    end

    local sky_holes = {}
    for _, c in ipairs(cells) do
        local cx, cz = (c.x0 + c.x1) * 0.5, (c.z0 + c.z1) * 0.5
        if c.kind == "flood" then
            local x0, z0, x1, z1 = c.hole[1], c.hole[2], c.hole[3], c.hole[4]
            local depth = 0.45
            basin(ctx, x0, z0, x1, z1, depth, -0.1, { tile = "pool_tile_pale" })
            -- a submerged step runs around the edge so you can wade in anywhere
            aabb(ctx, "pool_tile_pale", x0, -depth, z0, x1, -0.22, z0 + 0.6, true, "wade_step")
            aabb(ctx, "pool_tile_pale", x0, -depth, z1 - 0.6, x1, -0.22, z1, true, "wade_step")
            aabb(ctx, "pool_tile_pale", x0, -depth, z0 + 0.6, x0 + 0.6, -0.22, z1 - 0.6, true, "wade_step")
            aabb(ctx, "pool_tile_pale", x1 - 0.6, -depth, z0 + 0.6, x1, -0.22, z1 - 0.6, true, "wade_step")
            for px = x0 + 3.0, x1 - 2.5, 4.0 do
                for pz = z0 + 3.0, z1 - 2.5, 4.0 do
                    if rnd() < 0.8 then
                        aabb(ctx, "pool_tile", px - 0.3, -depth, pz - 0.3, px + 0.3, h, pz + 0.3, true, "pillar")
                    end
                end
            end
            if rnd() < 0.6 then
                sky_holes[#sky_holes + 1] = { cx - 2.5, cz - 2.5, cx + 2.5, cz + 2.5 }
            end
        elseif c.kind == "deep" then
            local x0, z0, x1, z1 = c.hole[1], c.hole[2], c.hole[3], c.hole[4]
            local depth = 1.6 + rnd() * 0.8
            basin(ctx, x0, z0, x1, z1, depth, -0.12, { band = true, coping = true })
            pool_ladder(ctx, "x", z0, 1, x1 - 1.4)
            if rnd() < 0.6 then
                pool_ladder(ctx, "x", z1, -1, x0 + 1.4)
            end
            -- steps down one short side so you are never trapped in the water
            local steps = math.floor(depth / 0.3 + 0.5)
            local rise = depth / steps
            for s = 1, steps - 1 do
                aabb(ctx, "pool_tile_aqua", x0 + 0.35 * (s - 1), -depth, z0 + 2.0, x0 + 0.35 * s, -rise * s, z1 - 2.0, true, "pool_step")
                aabb(ctx, "lane_blue", x0 + 0.35 * s - 0.05, -rise * s, z0 + 2.0, x0 + 0.35 * s, -rise * s + 0.004, z1 - 2.0, false, "step_nosing")
            end
            for x = x0 + 2.0, x1 - 1.9, 3.0 do
                make_light(ctx, disc(ctx, "pool_light", "x", z0, 1, x, -0.8, 0.14, 0.03, "underwater_light"), fixtures.underwater, 0.0)
                make_light(ctx, disc(ctx, "pool_light", "x", z1, -1, x, -0.8, 0.14, 0.03, "underwater_light"), fixtures.underwater, 0.0, 0x82)
            end
            sky_holes[#sky_holes + 1] = { x0 + 1.0, z0 + 1.0, x1 - 1.0, z1 - 1.0 }
        elseif c.kind == "dry" then
            -- a raised hot tub, empty rooms around it
            local tx0, tz0, tx1, tz1 = cx - 1.7, cz - 1.7, cx + 1.7, cz + 1.7
            local rim = 0.55
            aabb(ctx, "pool_tile", tx0, 0, tz0, tx1, rim, tz0 + 0.25, true, "tub_wall")
            aabb(ctx, "pool_tile", tx0, 0, tz1 - 0.25, tx1, rim, tz1, true, "tub_wall")
            aabb(ctx, "pool_tile", tx0, 0, tz0 + 0.25, tx0 + 0.25, rim, tz1 - 0.25, true, "tub_wall")
            aabb(ctx, "pool_tile", tx1 - 0.25, 0, tz0 + 0.25, tx1, rim, tz1 - 0.25, true, "tub_wall")
            aabb(ctx, "pool_tile_aqua", tx0 + 0.25, 0, tz0 + 0.25, tx1 - 0.25, 0.04, tz1 - 0.25, false, "tub_floor")
            aabb(ctx, "pool_tile_aqua", tx0 + 0.25, 0.04, tz0 + 0.25, tx0 + 0.7, 0.28, tz1 - 0.25, false, "tub_bench")
            aabb(ctx, "pool_water", tx0 + 0.25, rim - 0.1, tz0 + 0.25, tx1 - 0.25, rim - 0.08, tz1 - 0.25, false, "water")
            aabb(ctx, "coping", tx0 - 0.05, rim, tz0 - 0.05, tx1 + 0.05, rim + 0.04, tz0 + 0.25, false, "tub_coping")
            aabb(ctx, "coping", tx0 - 0.05, rim, tz1 - 0.25, tx1 + 0.05, rim + 0.04, tz1 + 0.05, false, "tub_coping")
            aabb(ctx, "coping", tx0 - 0.05, rim, tz0 + 0.25, tx0 + 0.25, rim + 0.04, tz1 - 0.25, false, "tub_coping")
            aabb(ctx, "coping", tx1 - 0.25, rim, tz0 + 0.25, tx1 + 0.05, rim + 0.04, tz1 - 0.25, false, "tub_coping")
            make_light(ctx, disc(ctx, "pool_light", "x", tz1 - 0.25, -1, cx, 0.22, 0.1, 0.02, "underwater_light"), fixtures.underwater, 0.0, 0x83)
            -- tiled ledges along the cell walls
            aabb(ctx, "pool_tile", c.x0 + 2.0, 0, c.z0 + 0.3, c.x1 - 2.0, 0.45, c.z0 + 0.8, true, "ledge")
            if rnd() < 0.5 then
                aabb(ctx, "pool_tile", c.x0 + 0.3, 0, c.z0 + 2.0, c.x0 + 0.8, 0.45, c.z1 - 2.0, true, "ledge")
            end
        else
            -- stairs up to a door that opens onto nothing
            local sx0, sx1 = cx - 1.25, cx + 1.25
            local sz0 = cz - 3.0
            local steps, rise, run = 8, 0.2, 0.35
            for s = 1, steps do
                aabb(ctx, "pool_tile", sx0, 0, sz0 + run * (s - 1), sx1, rise * s, sz0 + run * s, true, "stair")
            end
            local lz0 = sz0 + run * steps
            local top = rise * steps
            aabb(ctx, "pool_tile", sx0, 0, lz0, sx1, top, lz0 + 1.6, true, "landing")
            wall(ctx, "pool_tile", "x", lz0 + 1.75, 0.3, sx0 - 0.4, sx1 + 0.4, 0, h, { { a = cx - 0.5, b = cx + 0.5, top = top + 2.1, sill = top } }, "void_wall")
            aabb(ctx, "void_black", cx - 0.5, top, lz0 + 1.9, cx + 0.5, top + 2.1, lz0 + 2.6, false, "void")
            aabb(ctx, "pool_tile", sx0 - 0.4, 0, lz0 + 1.9, cx - 0.5, h, lz0 + 2.9, true, "void_block")
            aabb(ctx, "pool_tile", cx + 0.5, 0, lz0 + 1.9, sx1 + 0.4, h, lz0 + 2.9, true, "void_block")
            aabb(ctx, "pool_tile", cx - 0.5, 0, lz0 + 2.6, cx + 0.5, h, lz0 + 2.9, true, "void_block")
            aabb(ctx, "pool_tile", cx - 0.5, top + 2.1, lz0 + 1.9, cx + 0.5, h, lz0 + 2.6, false, "void_block")
            aabb(ctx, "pool_tile", cx - 0.5, 0, lz0 + 1.9, cx + 0.5, top, lz0 + 2.6, true, "void_block")
            railing(ctx, "chrome", sx0 - 0.05, lz0, sx0 - 0.05, lz0 + 1.5, top, 0.9, 0.75)
            railing(ctx, "chrome", sx1 + 0.05, lz0, sx1 + 0.05, lz0 + 1.5, top, 0.9, 0.75)
        end
    end

    -- tiled ceiling, daylight falls through square skylights, flush panels elsewhere
    slab_with_holes(ctx, "pool_tile", h, h + 0.3, 0, 0, CHUNK, CHUNK, sky_holes, false)
    for _, s in ipairs(sky_holes) do
        aabb(ctx, "sky_panel", s[1], h + 0.9, s[2], s[3], h + 0.95, s[4], false, "skylight")
        aabb(ctx, "pool_tile", s[1] - 0.3, h, s[2] - 0.3, s[1], h + 0.9, s[4] + 0.3, false, "skylight_well")
        aabb(ctx, "pool_tile", s[3], h, s[2] - 0.3, s[3] + 0.3, h + 0.9, s[4] + 0.3, false, "skylight_well")
        aabb(ctx, "pool_tile", s[1], h, s[2] - 0.3, s[3], h + 0.9, s[2], false, "skylight_well")
        aabb(ctx, "pool_tile", s[1], h, s[4], s[3], h + 0.9, s[4] + 0.3, false, "skylight_well")
        local mx, mz = (s[1] + s[3]) * 0.5, (s[2] + s[4]) * 0.5
        make_light(ctx, aabb(ctx, "sky_panel", mx - 0.01, h + 0.8, mz - 0.01, mx + 0.01, h + 0.81, mz + 0.01, false, "sky_probe"), fixtures.skylight, 2.0)
    end
    local function under_sky(x, z)
        for _, s in ipairs(sky_holes) do
            if x > s[1] - 1.0 and x < s[3] + 1.0 and z > s[2] - 1.0 and z < s[4] + 1.0 then
                return true
            end
        end
        return false
    end
    for x = 4.0, CHUNK - 3.0, 8.0 do
        for z = 4.0, CHUNK - 3.0, 8.0 do
            if not under_sky(x, z) then
                make_light(ctx, aabb(ctx, "panel_cool", x - 0.6, h - 0.02, z - 0.6, x + 0.6, h, z + 0.6, false, "flush_panel"), fixtures.poolroom, 0.3)
            end
        end
    end
end

local function build_pool(ctx)
    if hash_unit(ctx.cx, ctx.cz, 0x800) < 0.4 then
        build_natatorium(ctx)
    else
        build_poolrooms(ctx)
    end
end

--------------------------------------------------------------------------------
-- program registry, edge rules are how two chunks of the same building join up
--------------------------------------------------------------------------------

register("backrooms", { weight = 2.0, ceiling = 2.75, edge = { width = 3.0, height = 2.3 },  exit_signs = false, build = build_backrooms, acoustic = { 0.40, 0.32, 0.14 }, hum = { 0.075, 0.97 } })
register("office",    { weight = 2.0, ceiling = 2.7,  edge = { width = 16.0, height = 2.8 }, exit_signs = true,  build = build_office,    acoustic = { 0.45, 0.34, 0.12 }, hum = { 0.06, 1.0 } })
register("hotel",     { weight = 2.0, ceiling = 2.55, edge = { width = 2.2, height = 2.6 },  exit_signs = true,  build = build_hotel,     acoustic = { 0.30, 0.22, 0.08 }, hum = { 0.035, 0.9 } })
register("school",    { weight = 1.5, ceiling = 3.0,  edge = { width = 3.2, height = 3.1 },  exit_signs = true,  build = build_school,    acoustic = { 0.55, 0.50, 0.22 }, hum = { 0.06, 1.04 } })
register("garage",    { weight = 1.5, ceiling = 2.7,  edge = { open = true },                exit_signs = true,  build = build_garage,    acoustic = { 0.80, 0.62, 0.30 }, hum = { 0.05, 0.86 } })
register("mall",      { weight = 1.5, ceiling = 10.5, edge = { width = 12.0, height = 10.6 }, exit_signs = true, build = build_mall,      acoustic = { 0.90, 0.70, 0.34 }, hum = { 0.045, 0.93 } })
register("pool",      { weight = 2.0, ceiling = 3.6,  edge = { width = 6.0, height = 3.0 },   exit_signs = true, build = build_pool,      acoustic = { 0.95, 0.85, 0.45 }, hum = { 0.03, 0.82 } })

-- read only, lets tools find where a program is without streaming the world
function liminal.program_name_at(cx, cz)
    return program_at(cx, cz).name
end

--------------------------------------------------------------------------------
-- chunk build and streaming
--------------------------------------------------------------------------------

local function build_chunk(cx, cz)
    local program = program_at(cx, cz)
    local ctx =
    {
        cx = cx, cz = cz, ox = cx * CHUNK, oz = cz * CHUNK,
        jobs = {}, fixtures = {}, clones = {},
        program = program,
        rnd = prng_new(hash_u32(cx, cz, 0x77)),
        fx = 1, fz = 1, q = 0,
    }
    local neighbours =
    {
        W = program_at(cx - 1, cz), E = program_at(cx + 1, cz),
        S = program_at(cx, cz - 1), N = program_at(cx, cz + 1),
    }
    ctx.edges = {}
    for side, other in pairs(neighbours) do
        local spec = edge_spec(program, other)
        ctx.edges[side] = { width = spec.width, height = spec.height, open = spec.open, door = spec.door, other = other }
    end
    program.build(ctx)
    return ctx
end

local function spawn_job(job, root)
    local entity = World.CreateEntity()
    entity:SetName(job.label or "part")
    entity:SetTransient(true)
    entity:SetParent(root)
    -- a door can be swung before its chunk finishes spawning
    local door = job.door
    if door then
        door.entity = entity
        job.px, job.pz, job.yaw = door_pose(door, door.angle)
    end

    -- chunk roots sit at the origin so local and world space are the same
    entity:SetPositionLocal(Vector3(job.px, job.py, job.pz))
    entity:SetScaleLocal(Vector3(job.sx, job.sy, job.sz))
    if job.yaw or job.pitch or job.roll then
        entity:SetRotationLocal(Quaternion.FromEulerAngles(job.pitch or 0.0, job.yaw or 0.0, job.roll or 0.0))
    end

    local render = entity:AddComponent(ComponentType.Render)
    render:SetMesh(job.mesh or MeshType.Cube)
    local material = materials[job.material]
    if material then
        render:SetMaterial(material)
    end
    -- a water sheet in the tlas occludes every ray traced light below it and the basin goes black
    local def = material_defs[job.material]
    if def and def.water then
        render:SetFlag(RenderFlags.CastsShadows, false)
        render:SetFlag(RenderFlags.ExcludeFromRayTracing, true)
    end
    if job.fixture then
        job.fixture.render = render
    end

    -- physics reads the render bounds, so the mesh and the scale must already be set
    if job.physics then
        local physics = entity:AddComponent(ComponentType.Physics)
        physics:SetBodyType(BodyType.Box)
    end
end

local function add_mesh_physics(entity)
    local render = entity:GetComponent(ComponentType.Render)
    if render and not entity:GetComponent(ComponentType.Physics) then
        local physics = entity:AddComponent(ComponentType.Physics)
        physics:SetBodyType(BodyType.Mesh)
    end
end

local function get_template(name)
    if templates[name] == nil then
        templates[name] = World.GetEntityByName(name .. "_template") or false
    end
    return templates[name] or nil
end

local function spawn_clones(list, root)
    for _, c in ipairs(list) do
        local template = get_template(c.template)
        if template then
            local entity = template:Clone()
            entity:SetName(c.template)
            entity:SetTransient(true)
            entity:SetActive(true)
            entity:SetParent(root)
            entity:SetPositionLocal(Vector3(c.px, c.py, c.pz))
            entity:SetRotationLocal(Quaternion.FromEulerAngles(0.0, c.yaw, 0.0))
            entity:SetScaleLocal(Vector3(1.0, 1.0, 1.0))
            add_mesh_physics(entity)
            entity:ForEachDescendant(add_mesh_physics)
        end
    end
end

--------------------------------------------------------------------------------
-- walk grid and doors, rasterized from the physics jobs of every queued chunk
--------------------------------------------------------------------------------

local NAV_CELL     = 0.5
local NAV_N        = 96      -- cells per chunk side
local NAV_GROW     = 0.15    -- body margin around blockers
local nav_chunks   = {}
local moving_doors = {}

local function nav_chunk_id(cx, cz)
    return (cx + 32768) * 65536 + (cz + 32768)
end

-- visits every cell whose centre lies inside the job's footprint grown by grow
local function nav_cover(nav, job, grow, fn)
    local hx, hz = job.sx * 0.5 + grow, job.sz * 0.5 + grow
    local ux, uz = 1.0, 0.0
    local ex, ez = hx, hz
    if job.yaw and job.yaw ~= 0 then
        local r = math.rad(job.yaw)
        ux, uz = math.cos(r), -math.sin(r)
        ex = math.abs(ux) * hx + math.abs(uz) * hz
        ez = math.abs(uz) * hx + math.abs(ux) * hz
    end
    local x0 = math.max(0, math.floor((job.px - ex - nav.x0) / NAV_CELL))
    local x1 = math.min(NAV_N - 1, math.floor((job.px + ex - nav.x0) / NAV_CELL))
    local z0 = math.max(0, math.floor((job.pz - ez - nav.z0) / NAV_CELL))
    local z1 = math.min(NAV_N - 1, math.floor((job.pz + ez - nav.z0) / NAV_CELL))
    for gz = z0, z1 do
        local wz = nav.z0 + (gz + 0.5) * NAV_CELL - job.pz
        for gx = x0, x1 do
            local wx = nav.x0 + (gx + 0.5) * NAV_CELL - job.px
            if math.abs(wx * ux + wz * uz) <= hx and math.abs(wz * ux - wx * uz) <= hz then
                fn(gz * NAV_N + gx + 1)
            end
        end
    end
end

-- walk 0 is open, 1 blocked, 2 a doorway; floor holds the walking height, cells without one are holes
-- the band 0.3 to 1.9 m is what a body collides with, ramps are skipped and upper storeys ignored
local function build_nav(ctx)
    local nav = { x0 = ctx.ox, z0 = ctx.oz, walk = {}, floor = {}, doors = {} }
    local walk, floor = nav.walk, nav.floor
    for i = 1, NAV_N * NAV_N do
        walk[i] = 0
    end

    local function block(i)
        walk[i] = 1
    end
    for _, job in ipairs(ctx.jobs) do
        if job.physics and not job.roll and not job.door then
            local y0, y1 = job.py - job.sy * 0.5, job.py + job.sy * 0.5
            if y1 <= 0.3 and y1 >= -2.6 then
                nav_cover(nav, job, 0, function(i)
                    if not floor[i] or y1 > floor[i] then
                        floor[i] = y1
                    end
                end)
            elseif y0 < 1.9 and y1 > 0.3 then
                nav_cover(nav, job, NAV_GROW, block)
            end
        end
    end
    for _, c in ipairs(ctx.clones) do
        local size = c.template == "table" and 1.1 or 0.6
        nav_cover(nav, { px = c.px, pz = c.pz, sx = size, sz = size }, NAV_GROW, block)
    end

    for _, d in ipairs(ctx.doors or {}) do
        d.nav = nav
        d.key = string.format("%.1f:%.1f", d.cx, d.cz)
        nav_cover(nav, { px = d.cx, pz = d.cz, sx = d.width, sz = 0.6, yaw = d.closed_yaw }, 0, function(i)
            if walk[i] == 0 then
                walk[i] = 2
                nav.doors[i] = d
            end
        end)
    end
    return nav
end

local function nav_cell(gx, gz)
    local cx, cz = gx // NAV_N, gz // NAV_N
    local nav = nav_chunks[nav_chunk_id(cx, cz)]
    if not nav then
        return nil
    end
    return nav, (gz - cz * NAV_N) * NAV_N + (gx - cx * NAV_N) + 1
end

-- traversal cost, floor height and door of a cell, nil when a body cannot stand there
local function nav_walk(gx, gz)
    local nav, i = nav_cell(gx, gz)
    if not nav then
        return nil
    end
    local w, f = nav.walk[i], nav.floor[i]
    if w == 1 or not f then
        return nil
    end
    if w == 2 then
        local d = nav.doors[i]
        return (d and not d.open) and 4 or 1, f, d
    end
    return 1, f
end

local function nav_resident(x, z)
    return nav_chunks[nav_chunk_id(math.floor(x / CHUNK), math.floor(z / CHUNK))] ~= nil
end

local function doors_near(x, z, radius)
    local list = {}
    local r2 = radius * radius
    for _, chunk in pairs(chunks) do
        local ox, oz = chunk.cx * CHUNK, chunk.cz * CHUNK
        if x > ox - radius and x < ox + CHUNK + radius and z > oz - radius and z < oz + CHUNK + radius then
            for _, d in ipairs(chunk.doors) do
                local dx, dz = d.cx - x, d.cz - z
                if dx * dx + dz * dz <= r2 then
                    list[#list + 1] = d
                end
            end
        end
    end
    return list
end

-- a swinging leaf has no collider, it is rebuilt from the render bounds once the leaf settles
local function set_door(d, open, speed)
    d.open = open
    d.target = open and d.open_angle or 0
    d.speed = speed
    if d.entity and not moving_doors[d] then
        d.entity:RemoveComponent(ComponentType.Physics)
    end
    moving_doors[d] = true
end

local function update_doors(dt)
    for d in pairs(moving_doors) do
        local step = d.speed * dt
        local delta = d.target - d.angle
        if math.abs(delta) <= step then
            d.angle = d.target
        else
            d.angle = d.angle + (delta > 0 and step or -step)
        end
        if d.entity then
            local px, pz, yaw = door_pose(d, d.angle)
            d.entity:SetPositionLocal(Vector3(px, d.height * 0.5, pz))
            d.entity:SetRotationLocal(Quaternion.FromEulerAngles(0.0, yaw, 0.0))
        end
        if d.angle == d.target then
            moving_doors[d] = nil
            if d.entity then
                local physics = d.entity:AddComponent(ComponentType.Physics)
                physics:SetBodyType(BodyType.Box)
            end
        end
    end
end

local function queue_chunk(cx, cz)
    local key = chunk_key(cx, cz)
    if chunks[key] then
        return
    end

    local root = World.CreateEntity()
    root:SetName("chunk_" .. key)
    root:SetTransient(true)
    root:SetParent(host)
    root:SetPositionLocal(Vector3(0.0, 0.0, 0.0))
    root:SetActive(false)

    local ctx = build_chunk(cx, cz)
    local nav = build_nav(ctx)
    nav_chunks[nav_chunk_id(cx, cz)] = nav
    chunks[key] =
    {
        cx = cx, cz = cz, root = root, program = ctx.program, nav = nav, doors = ctx.doors or {},
        fixtures = ctx.fixtures, jobs = ctx.jobs, clones = ctx.clones, job_index = 1, ready = false,
    }
end

local function reveal_chunk(chunk)
    if chunk.ready then
        return
    end
    spawn_clones(chunk.clones, chunk.root)
    chunk.ready     = true
    chunk.jobs      = nil
    chunk.clones    = nil
    chunk.root:SetActive(true)
end

local function finish_chunk(chunk)
    while chunk.jobs and chunk.job_index <= #chunk.jobs do
        spawn_job(chunk.jobs[chunk.job_index], chunk.root)
        chunk.job_index = chunk.job_index + 1
    end
    reveal_chunk(chunk)
end

local function drain_queue(budget, ccx, ccz)
    -- the ring under the player must exist this frame, otherwise a sprint shows the builder
    for _, chunk in pairs(chunks) do
        if not chunk.ready and math.max(math.abs(chunk.cx - ccx), math.abs(chunk.cz - ccz)) <= liminal.view_radius then
            finish_chunk(chunk)
        end
    end

    local incomplete = {}
    for _, chunk in pairs(chunks) do
        if not chunk.ready then
            local dx, dz = chunk.cx - ccx, chunk.cz - ccz
            incomplete[#incomplete + 1] = { dx * dx + dz * dz, chunk }
        end
    end
    table.sort(incomplete, function(a, b) return a[1] < b[1] end)

    for i = 1, #incomplete do
        if budget <= 0 then
            break
        end
        local chunk = incomplete[i][2]
        while chunk.jobs and chunk.job_index <= #chunk.jobs and budget > 0 do
            spawn_job(chunk.jobs[chunk.job_index], chunk.root)
            chunk.job_index = chunk.job_index + 1
            budget = budget - 1
        end
        if chunk.job_index > #chunk.jobs then
            reveal_chunk(chunk)
        end
    end
end

local function update_residency(ccx, ccz)
    local r = liminal.view_radius + liminal.build_extra
    for cz = ccz - r, ccz + r do
        for cx = ccx - r, ccx + r do
            queue_chunk(cx, cz)
        end
    end

    local keep = r + liminal.keep_extra
    local stale = {}
    for key, chunk in pairs(chunks) do
        if math.abs(chunk.cx - ccx) > keep or math.abs(chunk.cz - ccz) > keep then
            stale[#stale + 1] = key
        end
    end
    for i = 1, #stale do
        local chunk = chunks[stale[i]]
        for _, d in ipairs(chunk.doors) do
            d.entity = nil
            moving_doors[d] = nil
        end
        nav_chunks[nav_chunk_id(chunk.cx, chunk.cz)] = nil
        World.RemoveEntity(chunk.root)
        chunks[stale[i]] = nil
    end
end

--------------------------------------------------------------------------------
-- lights, materials, audio
--------------------------------------------------------------------------------

-- emissive surfaces are everywhere but only pay off under path tracing, so a small pool of real
-- point lights is moved onto whichever live fixtures are nearest the camera
local function update_lights(pos, ccx, ccz)
    if #light_pool == 0 then
        return
    end

    local candidates = {}
    for _, chunk in pairs(chunks) do
        if chunk.ready and math.abs(chunk.cx - ccx) <= 1 and math.abs(chunk.cz - ccz) <= 1 then
            for _, f in ipairs(chunk.fixtures) do
                local dx, dz, dy = f[1] - pos.x, f[2] - pos.z, f[3] - pos.y
                candidates[#candidates + 1] = { dx * dx + dy * dy + dz * dz, f }
            end
        end
    end
    -- grid fixtures are often equidistant, the tie break keeps the ranking identical between updates
    table.sort(candidates, function(a, b)
        if a[1] ~= b[1] then
            return a[1] < b[1]
        end
        if a[2][1] ~= b[2][1] then
            return a[2][1] < b[2][1]
        end
        if a[2][2] ~= b[2][2] then
            return a[2][2] < b[2][2]
        end
        return a[2][3] < b[2][3]
    end)

    -- restir remembers lights by slot index, so a fixture that stays wanted keeps its slot and only
    -- the slots whose fixture dropped out move, reshuffling every slot turns all history into noise
    local wanted = {}
    for i = 1, math.min(#light_pool, #candidates) do
        wanted[candidates[i][2]] = true
    end
    local placed = {}
    for _, slot in ipairs(light_pool) do
        if slot.fixture and wanted[slot.fixture] then
            placed[slot.fixture] = true
        else
            slot.fixture = nil
        end
    end
    local next_candidate = 1
    for _, slot in ipairs(light_pool) do
        if not slot.fixture then
            while next_candidate <= #light_pool and candidates[next_candidate] and placed[candidates[next_candidate][2]] do
                next_candidate = next_candidate + 1
            end
            local c = next_candidate <= #light_pool and candidates[next_candidate] or nil
            if c then
                slot.fixture = c[2]
                placed[c[2]] = true
                slot.moved = true
            end
        end
    end

    for _, slot in ipairs(light_pool) do
        local f = slot.fixture
        if f then
            local spec = f.spec
            if slot.moved or not slot.active then
                slot.moved = false
                slot.active = true
                slot.entity:SetActive(true)
                slot.entity:SetPositionLocal(Vector3(f[1], f[3], f[2]))
            end
            local intensity = spec.lumens * f.level
            if slot.intensity ~= intensity then
                slot.intensity = intensity
                slot.light:SetIntensity(intensity)
            end
            if slot.range ~= spec.range then
                slot.range = spec.range
                slot.light:SetRange(spec.range)
            end
            if slot.temp ~= spec.temp then
                slot.temp = spec.temp
                slot.light:SetTemperature(spec.temp)
            end
        elseif slot.active ~= false then
            slot.active = false
            slot.entity:SetActive(false)
        end
    end
end

-- a small minority of ballasts falter briefly, with long quiet intervals
local function fixture_level(f, time)
    if f.spec.steady or hash_unit(f.gx, f.gz, 0x901) >= 0.07 then
        return 1
    end
    local clock = time + hash_unit(f.gx, f.gz, 0x902) * 61
    local cycle, phase = math.floor(clock / 61), clock % 61
    if hash_unit(f.gx, f.gz, 0x903 + cycle) >= 0.6 then
        return 1
    end
    if phase < 0.1 or (phase >= 0.22 and phase < 0.5) then
        return 0.15
    end
    return 1
end

local function update_flicker(dt)
    if not liminal.flicker then
        return
    end
    flicker_timer = flicker_timer + dt
    if flicker_timer < 0.04 then
        return
    end
    flicker_timer = 0
    local time = Timer.GetTimeSec()
    local sx, sz = stalker and stalker.active and stalker.x, stalker and stalker.active and stalker.z
    for _, chunk in pairs(chunks) do
        if chunk.ready then
            for _, f in ipairs(chunk.fixtures) do
                local level = fixture_level(f, time)
                -- ballasts starve near the entity, stuttering first and dying outright when it is under them
                if sx then
                    local dx, dz = f[1] - sx, f[2] - sz
                    local d2 = dx * dx + dz * dz
                    if d2 < 6.25 then
                        level = 0
                    elseif d2 < 144.0 then
                        local t = 1.0 - (math.sqrt(d2) - 2.5) / 9.5
                        if math.random() < 0.08 + 0.45 * t then
                            level = math.random() < t * 0.6 and 0 or 0.15
                        end
                    end
                end
                if level ~= f.level then
                    f.level = level
                    if f.render then
                        local m = materials[level == 1 and f.spec.on or (level == 0 and f.spec.off or f.spec.dim)]
                        if m then
                            f.render:SetMaterial(m)
                        end
                    end
                end
            end
        end
    end
end

local function create_material(name, def)
    local m = Material.New()
    m:SetResourceName("liminal_" .. name .. ".xml")
    local c = def.color or { 1, 1, 1, 1 }
    m:SetColor(c[1], c[2], c[3], c[4] or 1)
    if def.dir then
        m:SetTexture(MaterialTextureType.Color, def.dir .. "/albedo." .. def.ext)
        m:SetTexture(MaterialTextureType.Normal, def.dir .. "/normal." .. def.ext)
        m:SetTexture(MaterialTextureType.Roughness, def.dir .. "/roughness." .. def.ext)
        m:SetProperty(MaterialProperty.WorldSpaceUv, 1)
        m:SetProperty(MaterialProperty.TextureTilingX, def.tiling)
        m:SetProperty(MaterialProperty.TextureTilingY, def.tiling)
    else
        m:SetProperty(MaterialProperty.Roughness, def.roughness or 0.8)
        m:SetProperty(MaterialProperty.Metalness, def.metalness or 0)
    end
    if def.emissive then
        m:SetProperty(MaterialProperty.EmissiveFromAlbedo, 1)
    end
    -- water shades from refraction, both faces render so it reads from below the surface too
    if def.water then
        m:SetProperty(MaterialProperty.IsWater, 1)
        m:SetProperty(MaterialProperty.CullMode, 2)
    end
    return m
end

-- authored files load through a scratch render component, the cached pointer is reused by every part
local function load_materials()
    local probe = World.CreateEntity()
    probe:SetName("liminal_material_library")
    probe:SetTransient(true)
    probe:SetParent(host)
    probe:SetPositionLocal(Vector3(0.0, -1000.0, 0.0))
    probe:SetActive(false)
    local render = probe:AddComponent(ComponentType.Render)
    render:SetMesh(MeshType.Cube)

    for name, def in pairs(material_defs) do
        if def.file then
            render:SetDefaultMaterial()
            local before = render:GetMaterialName()
            render:SetMaterial(def.file)
            local loaded = render:GetMaterial()
            if loaded and render:GetMaterialName() ~= before then
                materials[name] = loaded
            elseif def.fallback then
                materials[name] = create_material(name, def.fallback)
            end
        else
            materials[name] = create_material(name, def)
        end
    end
end

local function create_light_pool()
    for i = 1, liminal.light_count do
        local entity = World.CreateEntity()
        entity:SetName("liminal_light_" .. i)
        entity:SetTransient(true)
        entity:SetParent(host)
        entity:SetPositionLocal(Vector3(0.0, 2.5, 0.0))
        entity:SetActive(false)

        local light = entity:AddComponent(ComponentType.Light)
        light:SetLightType(LightType.Point)
        light:SetTemperature(4000)
        light:SetIntensity(1500)
        light:SetRange(9)
        light:SetFlag(LightFlags.Shadows, false)
        light:SetFlag(LightFlags.Volumetric, liminal.light_volumetric)

        light_pool[i] = { entity = entity, light = light }
    end
end

local function update_audio(ccx, ccz, dt)
    local chunk = chunks[chunk_key(ccx, ccz)]
    if not chunk then
        return
    end
    local p = chunk.program
    local blend = 1 - math.exp(-math.min(dt, 0.25) * 0.8)

    if not hum_audio then
        local hum = World.GetEntityByName("fluorescent_hum")
        hum_audio = hum and hum:GetComponent(ComponentType.AudioSource) or false
    end
    if hum_audio then
        if not hum_audio:IsPlaying() then
            hum_audio:PlayClip()
        end
        hum_volume = hum_volume + (p.hum[1] - hum_volume) * blend
        hum_pitch = hum_pitch + (p.hum[2] - hum_pitch) * blend
        -- the mains sags and buzzes around the entity
        local presence = stalker and stalker.active and stalker.presence or 0
        local sag = presence * (0.28 + 0.08 * math.sin(Timer.GetTimeSec() * 7.3))
        hum_audio:SetVolume(math.min(1.0, hum_volume * (1.0 + 5.0 * presence)))
        hum_audio:SetPitch(hum_pitch * (1.0 - sag))
    end

    if not footsteps_audio then
        local body = World.GetEntityByName("physics_body_camera")
        footsteps_audio = body and body:GetComponent(ComponentType.AudioSource) or false
    end
    if footsteps_audio then
        acoustic_size = acoustic_size + (p.acoustic[1] - acoustic_size) * blend
        acoustic_decay = acoustic_decay + (p.acoustic[2] - acoustic_decay) * blend
        acoustic_wet = acoustic_wet + (p.acoustic[3] - acoustic_wet) * blend
        footsteps_audio:SetReverbEnabled(true)
        footsteps_audio:SetReverbRoomSize(acoustic_size)
        footsteps_audio:SetReverbDecay(acoustic_decay)
        footsteps_audio:SetReverbWet(acoustic_wet)
    end
end

--------------------------------------------------------------------------------
-- world queries shared with the stalker
--------------------------------------------------------------------------------

-- a few 3d voices moved to wherever a one shot sound happens
local function play_at(clip, x, y, z, volume, pitch)
    if #door_voices == 0 then
        for i = 1, 4 do
            local entity = World.CreateEntity()
            entity:SetName("liminal_voice_" .. i)
            entity:SetTransient(true)
            entity:SetParent(host)
            local audio = entity:AddComponent(ComponentType.AudioSource)
            audio:SetPlayOnStart(false)
            audio:SetLoop(false)
            audio:SetIs3d(true)
            door_voices[i] = { entity = entity, audio = audio }
        end
    end
    local voice = door_voices[door_voice_next]
    door_voice_next = door_voice_next % #door_voices + 1
    voice.entity:SetPositionLocal(Vector3(x, y, z))
    if voice.clip ~= clip then
        voice.clip = clip
        voice.audio:SetAudioClip(clip)
    end
    voice.audio:SetVolume(volume or 1.0)
    voice.audio:SetPitch(pitch or 1.0)
    voice.audio:PlayClip()
end

-- how well lit a point is by live fixtures, 0 is darkness and 1 is standing under a panel
local function light_at(x, y, z)
    local ccx, ccz = math.floor(x / CHUNK), math.floor(z / CHUNK)
    local best = 0
    for _, chunk in pairs(chunks) do
        if chunk.ready and math.abs(chunk.cx - ccx) <= 1 and math.abs(chunk.cz - ccz) <= 1 then
            for _, f in ipairs(chunk.fixtures) do
                if f.level > 0 then
                    local dx, dy, dz = f[1] - x, f[3] - y, f[2] - z
                    local reach = f.spec.range * 0.6
                    local d2 = dx * dx + dy * dy + dz * dz
                    if d2 < reach * reach then
                        best = math.max(best, f.level * (1.0 - math.sqrt(d2) / reach))
                    end
                end
            end
        end
    end
    return best
end

local function program_name_near(x, z)
    local chunk = chunks[chunk_key(math.floor(x / CHUNK), math.floor(z / CHUNK))]
    return chunk and chunk.program.name or nil
end

local function door_sound(d, clip, volume, pitch)
    play_at(clip, d.cx, 1.2, d.cz, volume, pitch)
end

local function stalker_door(d, open, violent)
    set_door(d, open, violent and 520 or 70)
    door_sound(d, violent and "project/liminal_space_resources/audio/door_slam.wav" or (open and "project/liminal_space_resources/audio/door_open.wav" or "project/liminal_space_resources/audio/door_close.wav"), violent and 1.0 or 0.8, violent and 1.0 or 0.8)
end

-- E swings the leaf the player is reaching for, the latch and the hinge are both audible
local function player_use_door(camera)
    local pos, fwd = camera:GetPosition(), camera:GetForward()
    local fl = math.sqrt(fwd.x * fwd.x + fwd.z * fwd.z)
    if fl < 0.01 then
        return
    end
    local fx, fz = fwd.x / fl, fwd.z / fl
    local best, best_d = nil, 2.2
    for _, d in ipairs(doors_near(pos.x, pos.z, 3.5)) do
        local lx, lz = door_pose(d, d.angle)
        for _, p in ipairs({ { lx, lz }, { d.cx, d.cz } }) do
            local dx, dz = p[1] - pos.x, p[2] - pos.z
            local dist = math.sqrt(dx * dx + dz * dz)
            local facing = dist > 0.01 and (dx * fx + dz * fz) / dist or 1
            if dist < best_d and (facing > 0.3 or dist < 0.9) then
                best, best_d = d, dist
            end
        end
    end
    if not best then
        return
    end
    local open = not best.open
    set_door(best, open, 150)
    door_sound(best, open and "project/liminal_space_resources/audio/door_open.wav" or "project/liminal_space_resources/audio/door_close.wav", 0.9, 1.0)
    if stalker and stalker.active then
        stalker.hear(best.cx, best.cz, open and 12.0 or 16.0, "door")
    end
end

local function make_stalker_api()
    return
    {
        cell = NAV_CELL,
        walk = nav_walk,
        resident = nav_resident,
        doors_near = doors_near,
        door_pose = door_pose,
        use_door = stalker_door,
        light_at = light_at,
        program_at = program_name_near,
        play_at = play_at,
    }
end

local function tick_stalker(dt)
    if not stalker then
        local ok, module = pcall(dofile, "../worlds/liminal_space_stalker.lua")
        if not ok then
            if stalker_error ~= module then
                stalker_error = module
                print("stalker failed to load: " .. tostring(module))
            end
            return
        end
        stalker = module
        stalker_api = make_stalker_api()
    end
    local ok, err = pcall(stalker.tick, stalker_api, dt)
    if not ok and stalker_error ~= err then
        stalker_error = err
        print("stalker error: " .. tostring(err))
    end
end

local function tick_soundscape(dt)
    if not soundscape then
        local ok, module = pcall(dofile, "../worlds/liminal_space_soundscape.lua")
        if not ok then
            if soundscape_error ~= module then
                soundscape_error = module
                print("soundscape failed to load: " .. tostring(module))
            end
            return
        end
        soundscape = module
        soundscape_api =
        {
            host = host,
            program_at = function(x, z)
                return program_at(math.floor(x / CHUNK), math.floor(z / CHUNK)).name
            end,
            presence = function()
                return stalker and stalker.active and stalker.presence or 0.0
            end,
        }
    end
    local ok, err = pcall(soundscape.tick, soundscape_api, dt)
    if not ok and soundscape_error ~= err then
        soundscape_error = err
        print("soundscape error: " .. tostring(err))
    end
end

--------------------------------------------------------------------------------
-- script entry points
--------------------------------------------------------------------------------

function liminal.Initialize(self, entity)
    if initialized then
        return
    end
    initialized = true

    host = entity
    -- a live script swap leaves the previous generator's transient children behind
    local leftovers = {}
    for _, child in ipairs(host:GetChildren()) do
        if child:IsTransient() then
            leftovers[#leftovers + 1] = child
        end
    end
    for i = 1, #leftovers do
        World.RemoveEntity(leftovers[i])
    end
    -- chunk children are placed in world space, so the generator must sit at the origin unrotated
    host:SetPosition(Vector3(0.0, 0.0, 0.0))
    host:SetRotation(Quaternion.Identity)
    host:SetScale(Vector3(1.0, 1.0, 1.0))

    if liminal.seed ~= 0 then
        layout_seed = liminal.seed & 0xffffffff
    else
        local a, b = math.randomseed()
        a = math.floor((tonumber(a) or 1) % 4294967296)
        b = math.floor((tonumber(b) or 1) % 4294967296)
        layout_seed = hash_u32(a, b, math.floor(Timer.GetTimeMs()))
        if layout_seed == 0 then
            layout_seed = 1
        end
    end
    program_cache = {}

    load_materials()
    create_light_pool()

    -- the starting block is built in one go, otherwise the player spawns into the void and falls
    local anchor = World.GetCameraEntity() or World.GetEntityByName("physics_body_camera")
    local pos = anchor and anchor:GetPosition() or Vector3(0.0, 0.0, 0.0)
    local ccx, ccz = math.floor(pos.x / CHUNK), math.floor(pos.z / CHUNK)
    last_ccx, last_ccz = ccx, ccz
    update_residency(ccx, ccz)
    for _, chunk in pairs(chunks) do
        if math.max(math.abs(chunk.cx - ccx), math.abs(chunk.cz - ccz)) <= liminal.view_radius then
            finish_chunk(chunk)
        end
    end
    update_lights(pos, ccx, ccz)
end

function liminal.Tick(self, entity)
    -- world loads call Initialize, a live file_path change only ever ticks
    if not initialized then
        liminal.Initialize(self, entity)
    end

    local dt = Timer.GetDeltaTimeSec()
    update_flicker(dt)

    local camera = World.GetCameraEntity()
    if not camera then
        return
    end

    local pos = camera:GetPosition()
    local ccx, ccz = math.floor(pos.x / CHUNK), math.floor(pos.z / CHUNK)

    stream_timer = stream_timer + dt
    if last_ccx ~= ccx or last_ccz ~= ccz or stream_timer >= liminal.stream_interval then
        stream_timer = 0.0
        last_ccx, last_ccz = ccx, ccz
        update_residency(ccx, ccz)
    end

    drain_queue(liminal.spawn_budget, ccx, ccz)

    if Input.GetKeyDown(KeyCode.E) then
        player_use_door(camera)
    end
    update_doors(dt)
    if liminal.stalker then
        tick_stalker(dt)
    end
    if liminal.soundscape then
        tick_soundscape(dt)
    end

    light_timer = light_timer + dt
    if light_timer >= 0.1 then
        light_timer = 0.0
        update_lights(pos, ccx, ccz)
    end
    update_audio(ccx, ccz, dt)
end

function liminal.Stop(self, entity)
    if stalker then
        pcall(stalker.shutdown)
    end
    if soundscape then
        pcall(soundscape.shutdown)
    end
end

return liminal
