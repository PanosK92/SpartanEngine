--------------------------------------------------------------------------------
-- plan tune shop
--
-- the island's tuning house, laid out like the tune shops of the old garage
-- racers: a column of departments, the parts each one sells, and a work order
-- on the right that explains the part, shows what it does to the car before it
-- is bought and fits it. parts are bought once and stay owned, so swapping
-- between owned stages is free.
--
-- the menu is opened from the main menu's developer quick jump for now, later
-- from the shop building itself. it reads and fits parts through CarTuning,
-- the simulation rebuilds the car with every fitted part.
--------------------------------------------------------------------------------

local shop = {}

local font_display = "data/fonts/OpenSans/OpenSans-ExtraBoldItalic.ttf"
local font_light   = "data/fonts/OpenSans/OpenSans-Light.ttf"
local font_medium  = "data/fonts/OpenSans/OpenSans-SemiBold.ttf"

local accent = { 1.0, 0.58, 0.20 }
local good   = { 0.45, 0.90, 0.55 }
local bad    = { 1.00, 0.42, 0.36 }

local starting_credits = 500000
local fit_time         = 0.75

--------------------------------------------------------------------------------
-- catalogue
--------------------------------------------------------------------------------

-- stats a work order can compare, better says which way is an improvement
local stat_rows =
{
    power        = { label = "POWER",          better = 1,  get = function(st) return st.power_kw * 1.341 end, fmt = function(v) return string.format("%d HP", math.floor(v + 0.5)) end },
    torque       = { label = "TORQUE",         better = 1,  get = function(st) return st.torque_nm end, fmt = function(v) return string.format("%d NM", math.floor(v + 0.5)) end },
    weight       = { label = "WEIGHT",         better = -1, get = function(st) return st.mass_kg end, fmt = function(v) return string.format("%d KG", math.floor(v + 0.5)) end },
    ratio        = { label = "POWER TO WEIGHT", better = -1, get = function(st) return st.mass_kg / math.max(st.power_kw * 1.341, 1) end, fmt = function(v) return string.format("%.2f KG/HP", v) end },
    redline      = { label = "REV LIMIT",      better = 1,  get = function(st) return st.redline_rpm end, fmt = function(v) return string.format("%d RPM", math.floor(v + 0.5)) end },
    boost        = { label = "PEAK BOOST",     better = 1,  get = function(st) return st.boost_bar end, fmt = function(v) return v > 0 and string.format("%.2f BAR", v) or "NONE" end },
    displacement = { label = "DISPLACEMENT",   better = 1,  get = function(st) return st.displacement_l end, fmt = function(v) return string.format("%.2f L", v) end },
    clutch       = { label = "CLUTCH CAPACITY", better = 1, get = function(st) return st.clutch_torque_nm end, fmt = function(v) return string.format("%d NM", math.floor(v + 0.5)) end },
    inertia      = { label = "ROTATING INERTIA", better = -1, get = function(st) return st.engine_inertia end, fmt = function(v) return string.format("%.3f KG M\xC2\xB2", v) end },
    shift        = { label = "SHIFT TIME",     better = -1, get = function(st) return st.shift_time end, fmt = function(v) return string.format("%d MS", math.floor(v * 1000 + 0.5)) end },
    final_drive  = { label = "FINAL DRIVE",    better = 0,  get = function(st) return st.final_drive end, fmt = function(v) return string.format("%.2f : 1", v) end },
    diff         = { label = "DIFFERENTIAL",   better = 0,  get = function(st) return st.diff_type end, fmt = function(v) return ({ [0] = "OPEN", [1] = "LOCKED", [2] = "LIMITED SLIP" })[v] or "?" end },
    grip         = { label = "PEAK GRIP",      better = 1,  get = function(st) return st.tire_friction end, fmt = function(v) return string.format("%.2f \xC2\xB5", v) end },
    brake_force  = { label = "BRAKE FORCE",    better = 1,  get = function(st) return st.brake_force end, fmt = "relative" },
    brake_bias   = { label = "BRAKE BALANCE",  better = 0,  get = function(st) return st.brake_bias_front end, fmt = function(v) return string.format("%d%% FRONT", math.floor(v * 100 + 0.5)) end },
    springs      = { label = "SPRING RATE",    better = 1,  get = function(st) return st.spring_front_hz end, fmt = function(v) return string.format("%.2f HZ", v) end },
    downforce    = { label = "DOWNFORCE",      better = 1,  get = function(st) return -(st.lift_front + st.lift_rear) end, fmt = function(v) return string.format("%.2f CL", v) end },
    yaw          = { label = "YAW CONTROL",    better = 1,  get = function(st) return st.yaw_control and 1 or 0 end, fmt = function(v) return v > 0 and "ACTIVE" or "OFF" end },
}

local departments =
{
    {
        id = "engine", label = "ENGINE",
        parts =
        {
            {
                key = "engine", name = "Engine Tune",
                stages = { "STREET", "SPORT", "RACE" }, prices = { 8500, 24000, 62000 },
                text = "A staged rebuild of the engine. Hotter camshafts with more lift and overlap, tighter tolerances and a fuel and ignition map written for power instead of economy. Every stage adds torque across the whole band, and the later stages let the engine spin further before the limiter cuts in.",
                rows = { "power", "torque", "redline", "ratio" },
            },
            {
                key = "ecu", name = "Sports ECU",
                stages = { "PROGRAMMABLE" }, prices = { 4200 },
                text = "The factory engine computer is tuned for emissions, refinement and fuel of unknown quality. A programmable unit is mapped for performance fuel: more ignition advance, richer fuelling at full load and a rev limiter moved a little higher.",
                rows = { "power", "torque", "redline" },
            },
            {
                key = "port_polish", name = "Port & Polish",
                stages = { "HAND FINISHED" }, prices = { 6800 },
                text = "The intake and exhaust ports in the cylinder head are reshaped and smoothed by hand, so air flows into and out of each cylinder with less turbulence. A small gain, but it comes everywhere, mostly in the upper half of the rev range.",
                rows = { "power", "torque" },
            },
            {
                key = "engine_balance", name = "Engine Balancing",
                stages = { "BLUEPRINTED" }, prices = { 9500 },
                text = "Pistons, rods and crankshaft are weighed and machined until they match within a fraction of a gram. A balanced rotating assembly vibrates less, loses less to internal friction and is happy to rev higher.",
                rows = { "power", "inertia", "redline" },
            },
            {
                key = "displacement", name = "Displacement Increase",
                stages = { "BORE & STROKE" }, prices = { 18000 },
                text = "Larger pistons and a longer throw crankshaft give every cycle more air and fuel to burn. The most direct way to add torque, and the engine note deepens with it.",
                rows = { "power", "torque", "displacement" },
            },
        },
    },
    {
        id = "induction", label = "INDUCTION",
        parts =
        {
            {
                key = "intake", name = "Air Intake",
                stages = { "SPORT FILTER", "COLD AIR BOX", "RAM AIR" }, prices = { 1200, 3600, 7500 },
                text = "A free flowing filter element and, at higher stages, a sealed cold air box fed from the front of the car. Cooler, denser air reaches the throttle with less restriction, and the induction roar becomes part of the soundtrack.",
                rows = { "power", "torque" },
            },
            {
                key = "turbo", name = "Turbo Kit",
                stages = { "LOW BOOST", "MID BOOST", "HIGH BOOST", "RACE" }, prices = { 25000, 48000, 85000, 140000 },
                text = "On a naturally aspirated engine this fits a complete turbocharger system, manifold, oil feed and wastegate, set to a mild street boost. On an engine that is already boosted it moves to a larger compressor. Each stage raises peak boost and spools faster, at the price of a softer response before the turbo wakes up.",
                rows = { "power", "torque", "boost", "ratio" },
            },
            {
                key = "intercooler", name = "Intercooler",
                stages = { "SPORT CORE", "RACING CORE" }, prices = { 6000, 14000 },
                text = "Air leaves a compressor hot, and hot air holds less oxygen. A larger intercooler core cools the charge before it reaches the cylinders, so every bar of boost makes more power. The racing core sheds enough heat for the wastegate to hold a little more boost.",
                rows = { "power", "torque", "boost" },
            },
        },
    },
    {
        id = "exhaust", label = "EXHAUST",
        parts =
        {
            {
                key = "exhaust", name = "Muffler",
                stages = { "SPORT", "SEMI-RACING", "RACING" }, prices = { 2800, 6500, 12000 },
                text = "The restrictive factory silencer is swapped for freer flowing designs. Back pressure drops, the engine breathes out more easily and the car gets louder with every stage. The racing muffler is close to a straight pipe.",
                rows = { "power", "torque" },
            },
            {
                key = "exhaust_manifold", name = "Exhaust Manifold",
                stages = { "EQUAL LENGTH HEADERS" }, prices = { 7200 },
                text = "Cast iron log manifolds are replaced by tubular headers with equal length primaries. The exhaust pulse from each cylinder now helps pull the next one out instead of colliding with it.",
                rows = { "power", "torque" },
            },
        },
    },
    {
        id = "drivetrain", label = "DRIVETRAIN",
        parts =
        {
            {
                key = "clutch", name = "Clutch",
                stages = { "SINGLE PLATE", "TWIN PLATE", "TRIPLE PLATE" }, prices = { 3400, 8200, 15500 },
                text = "Stronger springs and more friction surfaces let the clutch hold far more torque without slipping, and it bites quicker and harder. Essential once the engine makes real power, but harder to drive smoothly at walking pace.",
                rows = { "clutch", "power" },
            },
            {
                key = "flywheel", name = "Flywheel",
                stages = { "LIGHTWEIGHT", "SEMI-RACING", "RACING" }, prices = { 2600, 5400, 9800 },
                text = "A lighter flywheel stores less energy, so the engine gains and loses revs faster. Throttle blips are sharper and the car pulls a touch harder in the low gears, but the idle is less steady and pulling away needs more care.",
                rows = { "inertia", "power" },
            },
            {
                key = "driveshaft", name = "Carbon Driveshaft",
                stages = { "CARBON FIBRE" }, prices = { 8500 },
                text = "A carbon fibre propeller shaft weighs about half as much as steel and twists less under load. Less rotating mass between gearbox and axle means a crisper answer to the throttle. Front wheel drive cars have no propeller shaft to replace.",
                rows = { "power" },
            },
            {
                key = "transmission", name = "Transmission",
                stages = { "CLOSE RATIO", "SUPER CLOSE", "FULLY ADJUSTABLE" }, prices = { 12000, 22000, 45000 },
                text = "The gear ratios are pulled closer together, so each upshift drops fewer revs and the engine stays near its power peak. Shifts get quicker at every stage, and the fully adjustable box lets you set the final drive: shorter for tight mountain roads, longer for top speed.",
                rows = { "shift", "final_drive" },
                setting = { key = "final_drive", label = "FINAL DRIVE", min = 0.8, max = 1.25, step = 0.01, min_stage = 3,
                            fmt = function(v) return string.format("x%.2f   %s", v, v > 1.005 and "SHORTER" or (v < 0.995 and "LONGER" or "STOCK")) end },
            },
            {
                key = "lsd", name = "Limited-Slip Differential",
                stages = { "1-WAY", "1.5-WAY", "2-WAY" }, prices = { 9000, 11500, 14000 },
                text = "An open differential sends power to whichever wheel has the least grip. A limited slip unit locks the driven wheels together under load. A 1-way locks only on power, a 1.5-way also locks gently off the throttle, and a 2-way locks equally both ways: the most stable, at some cost to turn in.",
                rows = { "diff" },
            },
            {
                key = "yaw_control", name = "Active Yaw Control",
                stages = { "ACTIVE" }, prices = { 16000 },
                text = "Sensors compare where the car is pointing with where it is being steered, and the system brakes individual wheels to help the car rotate or to tidy up a slide. Built for all wheel drive cars.",
                rows = { "yaw" },
            },
        },
    },
    {
        id = "suspension", label = "SUSPENSION",
        parts =
        {
            {
                key = "suspension", name = "Suspension Kit",
                stages = { "SPORT", "SEMI-RACING", "RACING" }, prices = { 6000, 14000, 28000 },
                text = "Stiffer springs and anti-roll bars cut body roll and pitch, so the tires stay flatter on the road. Each stage sharpens turn in and response. The racing kit is made for smooth tarmac and rides hard over broken roads.",
                rows = { "springs" },
            },
            {
                key = "rigidity", name = "Chassis Reinforcement",
                stages = { "SEAM WELD & BRACING" }, prices = { 18000 },
                text = "The body shell is seam welded and braced, and the soft rubber suspension bushings give way to much firmer ones. The suspension pickup points stop moving under load, so the car answers every input more precisely.",
                rows = { "springs" },
            },
        },
    },
    {
        id = "brakes", label = "BRAKES",
        parts =
        {
            {
                key = "brakes", name = "Brakes",
                stages = { "SPORT PADS", "SPORT KIT", "RACING KIT" }, prices = { 3500, 9000, 22000 },
                text = "Grippier pads, then larger discs and multi piston calipers. Braking torque rises, the discs shed heat faster and the pedal stays firm lap after lap instead of fading.",
                rows = { "brake_force" },
            },
            {
                key = "brake_controller", name = "Brake Balance Controller",
                stages = { "ADJUSTABLE" }, prices = { 4800 },
                text = "A cockpit adjustable valve that sets how braking force splits between the front and rear axles. More rear helps the car rotate on the way into a corner, more front keeps it stable but pushes it wide.",
                rows = { "brake_bias" },
                setting = { key = "brake_bias", label = "BRAKE BALANCE", min = 0.4, max = 0.8, step = 0.01, min_stage = 1,
                            fmt = function(v) return string.format("%d%% FRONT   %d%% REAR", math.floor(v * 100 + 0.5), 100 - math.floor(v * 100 + 0.5)) end },
            },
        },
    },
    {
        id = "tires", label = "TIRES",
        parts =
        {
            {
                key = "tires", name = "Tires",
                stages = { "SPORT", "SEMI-SLICK", "RACING SLICK" }, prices = { 4000, 9500, 18000 },
                text = "Softer compounds with less tread pattern. Grip rises with every step and stiffer carcasses sharpen the steering, but the working temperature climbs too: racing rubber needs a few hard corners before it really bites.",
                rows = { "grip" },
            },
        },
    },
    {
        id = "body", label = "BODY",
        parts =
        {
            {
                key = "weight", name = "Weight Reduction",
                stages = { "STAGE 1", "STAGE 2", "STAGE 3" }, prices = { 5000, 12000, 26000 },
                text = "Sound deadening, the spare wheel and trim come out first, then the rear seats and heavy glass, then everything that is not structural. Less mass makes the car faster everywhere: accelerating, braking and turning.",
                rows = { "weight", "ratio" },
            },
            {
                key = "aero", name = "Aero Kit",
                stages = { "SPLITTER & WING", "FULL AERO" }, prices = { 15000, 32000 },
                text = "A front splitter, a rear diffuser and a wing press the car into the road at speed. Grip in fast corners rises noticeably while slow corners feel the same.",
                rows = { "downforce" },
            },
        },
    },
}

-- why a car cannot take a part, keyed by part
local unavailable_reason =
{
    intercooler  = "Needs a turbocharged engine or a car that accepts a turbo kit",
    driveshaft   = "Front wheel drive cars have no propeller shaft",
    yaw_control  = "Only for all wheel drive cars",
    lsd          = "This car already runs a locked differential",
    transmission = "This gearbox cannot be re-geared",
}

--------------------------------------------------------------------------------
-- state
--------------------------------------------------------------------------------

local car           = nil
local car_name      = ""
local camera_entity = nil
local fonts_ready   = false
local time          = 0.0
local open_time     = 0.0
local closing       = false
local close_time    = 0.0
local department    = 1
local part_index    = 1
local cursor        = 0
local dept_anim     = {}
local part_anim     = {}
local toast_text    = nil
local toast_time    = 0.0
local toast_good    = true
local fitting       = nil
local wrap_cache    = {}
local stats_cache   = {}
local mouse_last_x  = -1
local mouse_last_y  = -1
local hit           = {}

local function wallet()
    if not _G.plan_garage then
        _G.plan_garage = { credits = starting_credits, owned = {} }
    end
    return _G.plan_garage
end

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local function clamp(x, a, b) return math.max(a, math.min(b, x)) end
local function lerp(a, b, t) return a + (b - a) * t end
local function smooth(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end
local function ease_out(t) t = clamp(t, 0, 1) return 1 - (1 - t) ^ 3 end
local function approach(current, target, rate, dt) return current + (target - current) * (1 - math.exp(-rate * dt)) end

local function white(a) return Vector4(1, 1, 1, a) end
local function black(a) return Vector4(0, 0, 0, a) end
local function amber(a) return Vector4(accent[1], accent[2], accent[3], a) end
local function tint(c, a) return Vector4(c[1], c[2], c[3], a) end

local function credits_text(value)
    local text = tostring(math.floor(value))
    while true do
        local replaced, count = text:gsub("^(%d+)(%d%d%d)", "%1,%2")
        text = replaced
        if count == 0 then
            break
        end
    end
    return text .. " CR"
end

local function inside(b, x, y)
    return b and x >= b[1] and x <= b[1] + b[3] and y >= b[2] and y <= b[2] + b[4]
end

local function wrap(key, text, width, size, font)
    local cache_key = key .. ":" .. math.floor(width) .. ":" .. math.floor(size)
    if wrap_cache[cache_key] then
        return wrap_cache[cache_key]
    end
    local lines, line = {}, ""
    for word in text:gmatch("%S+") do
        local candidate = line == "" and word or (line .. " " .. word)
        if line ~= "" and Hud.TextWidth(candidate, size, font) > width then
            table.insert(lines, line)
            line = word
        else
            line = candidate
        end
    end
    if line ~= "" then
        table.insert(lines, line)
    end
    wrap_cache[cache_key] = lines
    return lines
end

local function owned_table()
    local owned = wallet().owned
    owned[car_name] = owned[car_name] or {}
    return owned[car_name]
end

local function is_owned(part, stage)
    if stage == 0 then
        return true
    end
    local entry = owned_table()[part.key]
    return entry ~= nil and entry[stage] == true
end

local function stage_of(part)
    return math.max(CarTuning.GetStage(car, part.key), 0)
end

local function stage_max(part)
    return math.min(CarTuning.GetStageMax(car, part.key), #part.stages)
end

local function stage_name(part, stage)
    return stage == 0 and "STOCK" or (part.stages[stage] or ("STAGE " .. stage))
end

local function current_part()
    return departments[department].parts[part_index]
end

-- preview stats are cached per part and stage, fitting anything clears them
local function preview(part, stage)
    local key = part.key .. ":" .. stage
    if not stats_cache[key] then
        stats_cache[key] = CarTuning.PreviewStats(car, part.key, stage)
    end
    return stats_cache[key]
end

local function toast(text, ok)
    toast_text, toast_time, toast_good = text, 0, ok
end

--------------------------------------------------------------------------------
-- camera, a slow turntable sweep around the car
--------------------------------------------------------------------------------

local function create_camera()
    camera_entity = World.CreateEntity()
    camera_entity:SetName("tune_shop_camera")
    camera_entity:SetTransient(true)
    local camera = camera_entity:AddComponent(ComponentType.Camera)
    if camera then
        camera:SetFlag(CameraFlags.CanBeControlled, false)
        camera:SetFovHorizontalDeg(66)
    end
    World.SetActiveCamera(camera_entity)
end

local function release_camera()
    if camera_entity and World.GetActiveCameraOverride() == camera_entity then
        World.SetActiveCamera(nil)
    end
    if camera_entity then
        World.RemoveEntity(camera_entity)
        camera_entity = nil
    end
end

local function tick_camera()
    if not camera_entity or not car then
        return
    end
    local center  = car:GetPosition()
    local forward = car:GetForward()
    local right   = car:GetRight()
    local up      = Vector3(0, 1, 0)

    -- a three quarter front view that drifts around the nose and back
    local angle  = math.rad(38 + math.sin(time * 0.11) * 26)
    -- the garage is tight, further out the orbit ends up inside its walls
    local radius = 6.0
    local offset = forward * (math.cos(angle) * radius) + right * (math.sin(angle) * radius)
    local eye    = center + offset + up * (1.45 + math.sin(time * 0.17) * 0.1)
    -- aim past the car's far side so it sits in the clear gap between the two columns
    local side   = Vector3(offset.z, 0, -offset.x):Normalized()
    local target = center + up * 0.25 + side * 0.9
    camera_entity:SetPosition(eye)
    camera_entity:SetRotation(Quaternion.FromLookRotation(target - eye, up))
end

--------------------------------------------------------------------------------
-- actions
--------------------------------------------------------------------------------

local function fit(part, stage)
    if fitting or not car then
        return
    end
    local max = stage_max(part)
    if max <= 0 then
        toast(string.upper(unavailable_reason[part.key] or "Not available for this car"), false)
        return
    end
    if stage == stage_of(part) then
        toast(stage == 0 and "ALREADY STOCK" or "ALREADY FITTED", true)
        return
    end
    local garage = wallet()
    if not is_owned(part, stage) then
        local price = part.prices[stage] or 0
        if garage.credits < price then
            toast("NOT ENOUGH CREDITS   \xC2\xB7   " .. credits_text(price - garage.credits) .. " SHORT", false)
            return
        end
        garage.credits = garage.credits - price
        local owned = owned_table()
        owned[part.key] = owned[part.key] or {}
        owned[part.key][stage] = true
    end
    fitting = { part = part, stage = stage, time = 0 }
end

local function finish_fit()
    local part, stage = fitting.part, fitting.stage
    fitting = nil
    if CarTuning.SetStage(car, part.key, stage) then
        stats_cache = {}
        if stage == 0 then
            toast("REFITTED STOCK " .. string.upper(part.name), true)
        else
            toast("FITTED   \xC2\xB7   " .. stage_name(part, stage) .. " " .. string.upper(part.name), true)
        end
    else
        toast("THE WORKSHOP COULD NOT FIT THIS PART", false)
    end
end

local function adjust_setting(part, direction)
    local setting = part.setting
    if not setting or stage_of(part) < setting.min_stage then
        return
    end
    local value = CarTuning.GetSetting(car, setting.key)
    if setting.key == "final_drive" and value <= 0 then
        value = 1
    end
    value = clamp(value + direction * setting.step, setting.min, setting.max)
    if CarTuning.SetSetting(car, setting.key, value) then
        stats_cache = {}
    end
end

--------------------------------------------------------------------------------
-- drawing
--------------------------------------------------------------------------------

local function load_fonts()
    if fonts_ready then
        return
    end
    Hud.LoadFont("display", font_display)
    Hud.LoadFont("light", font_light)
    Hud.LoadFont("medium", font_medium)
    fonts_ready = true
end

local function draw_keycap(text, x, y, s, alpha)
    local tw = Hud.TextWidth(text, 15 * s, "mono")
    local cw = math.max(tw + 16 * s, 30 * s)
    Hud.Rect(x, y, cw, 28 * s, white(0.08 * alpha), 4 * s)
    Hud.RectOutline(x, y, cw, 28 * s, white(0.5 * alpha), 1.2 * s, 4 * s)
    Hud.Text(text, x + cw * 0.5, y + 5 * s, 15 * s, white(0.9 * alpha), "mono", 0.5)
    return cw
end

local function draw_backdrop(w, h, s, a)
    -- the car sits in the clear middle, both columns get a deep wash behind them
    Hud.RectGradient(0, 0, w * 0.50, h, black(0.88 * a), black(0.0), black(0.0), black(0.88 * a))
    Hud.Rect(0, 0, 820 * s, h, black(0.35 * a))
    Hud.RectGradient(w * 0.55, 0, w * 0.45, h, black(0.0), black(0.85 * a), black(0.85 * a), black(0.0))
    Hud.RectGradient(0, 0, w, 170 * s, black(0.7 * a), black(0.7 * a), black(0.0), black(0.0))
    Hud.RectGradient(0, h - 190 * s, w, 190 * s, black(0.0), black(0.0), black(0.85 * a), black(0.85 * a))
end

local function draw_header(w, h, s, a)
    local x = 80 * s
    Hud.Text("KARABELAS MOTORWORKS   \xC2\xB7   PERFORMANCE PARTS", x + 4 * s, 46 * s, 16 * s, amber(a), "mono", 0, 5 * s)
    Hud.Text("TUNE SHOP", x, 62 * s, 74 * s, white(a), "display", 0, 1 * s)

    local rx = w - 80 * s
    Hud.Text(string.upper(car_name), rx, 54 * s, 30 * s, white(0.95 * a), "medium", 1, 1 * s)
    Hud.Text("BALANCE", rx - Hud.TextWidth(credits_text(wallet().credits), 26 * s, "light", 1 * s) - 16 * s, 102 * s, 14 * s, white(0.5 * a), "mono", 1, 4 * s)
    Hud.Text(credits_text(wallet().credits), rx, 94 * s, 26 * s, amber(a), "light", 1, 1 * s)
end

local function draw_departments(s, a, dt)
    local x, y = 80 * s, 220 * s
    local row_h = 54 * s
    hit.departments = {}
    Hud.Text("DEPARTMENT", x, y - 34 * s, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
    for i, dept in ipairs(departments) do
        dept_anim[i] = approach(dept_anim[i] or 0, i == department and 1 or 0, 14, dt)
        local k  = dept_anim[i]
        local ry = y + (i - 1) * row_h
        local ra = ease_out((open_time - 0.1 - i * 0.04) / 0.5) * a
        if k > 0.01 then
            Hud.Rect(x - 16 * s, ry - 6 * s, 4 * s, (row_h - 12 * s) * k, amber(ra))
            Hud.RectGradient(x - 16 * s, ry - 6 * s, 220 * s * k, row_h - 12 * s, amber(0.16 * k * ra), amber(0.0), amber(0.0), amber(0.16 * k * ra))
        end
        Hud.Text(dept.label, x + 8 * s * k, ry + 4 * s, 19 * s, (i == department) and white(ra) or white(0.5 * ra), "medium", 0, 3 * s)

        -- a tick for every part in the department that has something fitted
        local fitted = 0
        for _, part in ipairs(dept.parts) do
            if stage_of(part) > 0 then
                fitted = fitted + 1
            end
        end
        if fitted > 0 then
            Hud.Text(tostring(fitted), x + 196 * s, ry + 7 * s, 15 * s, amber(ra), "mono", 1)
        end
        hit.departments[i] = { x - 16 * s, ry - 6 * s, 220 * s, row_h - 12 * s }
    end
end

local function draw_pips(part, x, y, s, a, current, max)
    for i = 1, #part.stages do
        local available = i <= max
        local col
        if i <= current then
            col = amber(a)
        elseif available and is_owned(part, i) then
            col = white(0.55 * a)
        elseif available then
            col = white(0.16 * a)
        else
            col = white(0.05 * a)
        end
        Hud.Rect(x + (i - 1) * 16 * s, y, 11 * s, 11 * s, col, 2 * s)
    end
end

local function draw_parts(s, a, dt)
    local dept = departments[department]
    local x, y = 330 * s, 220 * s
    local width = 450 * s
    local row_h = 76 * s
    hit.parts = {}
    Hud.Text(dept.label .. "   \xC2\xB7   " .. #dept.parts .. " PARTS", x, y - 34 * s, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
    for i, part in ipairs(dept.parts) do
        part_anim[i] = approach(part_anim[i] or 0, i == part_index and 1 or 0, 14, dt)
        local k   = part_anim[i]
        local ry  = y + (i - 1) * row_h
        local max = stage_max(part)
        local cur = stage_of(part)
        local ra  = a * (max > 0 and 1.0 or 0.45)

        Hud.Rect(x - 14 * s, ry - 8 * s, width, row_h - 10 * s, white((0.035 + 0.05 * k) * a), 4 * s)
        if k > 0.01 then
            Hud.RectOutline(x - 14 * s, ry - 8 * s, width, row_h - 10 * s, amber(0.75 * k * a), 1.5 * s, 4 * s)
        end
        Hud.Text(part.name, x, ry, 24 * s, white(lerp(0.75, 1.0, k) * ra), "medium", 0, 0.5 * s)

        local status
        if max <= 0 then
            status = "NOT AVAILABLE FOR THIS CAR"
        elseif cur > 0 then
            status = "FITTED   \xC2\xB7   " .. stage_name(part, cur)
        else
            status = "STOCK"
        end
        Hud.Text(status, x, ry + 36 * s, 13 * s, (cur > 0) and amber(0.9 * ra) or white(0.5 * ra), "mono", 0, 3 * s)
        if max > 0 then
            draw_pips(part, x + width - 40 * s - #part.stages * 16 * s, ry + 10 * s, s, ra, cur, max)
        end
        hit.parts[i] = { x - 14 * s, ry - 8 * s, width, row_h - 10 * s }
    end
end

local function draw_compare(part, x, y, width, s, a)
    local now  = preview(part, stage_of(part))
    local next = preview(part, cursor)
    local base = preview(part, 0)
    if not now or not next then
        return y
    end
    Hud.Text("WHAT CHANGES", x, y, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
    y = y + 28 * s
    for _, id in ipairs(part.rows) do
        local row = stat_rows[id]
        local v_now, v_next = row.get(now), row.get(next)
        local fmt = row.fmt
        if fmt == "relative" then
            local reference = math.max(row.get(base), 0.0001)
            fmt = function(v) return string.format("%d%%", math.floor(v / reference * 100 + 0.5)) end
        end
        Hud.Text(row.label, x, y + 3 * s, 14 * s, white(0.6 * a), "mono", 0, 2 * s)
        local delta = v_next - v_now
        local changed = math.abs(delta) > math.max(math.abs(v_now) * 0.0005, 0.00001)
        local col = white(0.9 * a)
        if changed and row.better ~= 0 then
            col = (delta * row.better > 0) and tint(good, a) or tint(bad, a)
        elseif changed then
            col = amber(a)
        end
        local text = fmt(v_now)
        if changed then
            text = fmt(v_now) .. "   \xE2\x86\x92   " .. fmt(v_next)
        end
        Hud.Text(text, x + width, y, 18 * s, changed and col or white(0.85 * a), "medium", 1, 0.5 * s)
        Hud.Rect(x, y + 30 * s, width, 1 * s, white(0.08 * a))
        y = y + 40 * s
    end
    return y
end

local function draw_setting(part, x, y, width, s, a)
    local setting = part.setting
    if not setting then
        return y
    end
    local unlocked = stage_of(part) >= setting.min_stage
    local value = CarTuning.GetSetting(car, setting.key)
    if setting.key == "final_drive" and value <= 0 then
        value = 1
    end
    Hud.Text(setting.label, x, y, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
    if not unlocked then
        Hud.Text("FIT " .. stage_name(part, setting.min_stage) .. " TO ADJUST", x + width, y - 1 * s, 13 * s, white(0.35 * a), "mono", 1, 2 * s)
    end
    y = y + 26 * s
    local t = (value - setting.min) / (setting.max - setting.min)
    local bar_a = unlocked and a or 0.3 * a
    Hud.Rect(x, y + 9 * s, width, 4 * s, white(0.15 * bar_a), 2 * s)
    Hud.Rect(x, y + 9 * s, width * t, 4 * s, amber(bar_a), 2 * s)
    Hud.Circle(x + width * t, y + 11 * s, 8 * s, unlocked and white(a) or white(0.3 * a))
    Hud.Text(setting.fmt(value), x, y + 24 * s, 16 * s, white(0.85 * bar_a), "mono", 0, 2 * s)
    hit.setting_minus = unlocked and { x, y - 6 * s, width * 0.5, 34 * s } or nil
    hit.setting_plus  = unlocked and { x + width * 0.5, y - 6 * s, width * 0.5, 34 * s } or nil
    return y + 60 * s
end

local function draw_detail(w, h, s, a)
    local part  = current_part()
    local width = 560 * s
    local x     = w - 80 * s - width
    local y     = 150 * s
    local pad   = 30 * s
    local inner = width - pad * 2
    local max   = stage_max(part)
    local cur   = stage_of(part)
    hit.stages, hit.fit = {}, nil

    Hud.Rect(x, y, width, h - y - 170 * s, black(0.55 * a), 6 * s)
    Hud.Rect(x, y, width, 4 * s, amber(a), 2 * s)

    local cy = y + 28 * s
    Hud.Text("WORK ORDER   \xC2\xB7   " .. departments[department].label, x + pad, cy, 13 * s, amber(a), "mono", 0, 4 * s)
    cy = cy + 22 * s
    Hud.Text(part.name, x + pad, cy, 40 * s, white(a), "display", 0, 0.5 * s)
    cy = cy + 66 * s

    -- the description of what the part is and does
    for _, line in ipairs(wrap(part.key, part.text, inner, 18 * s, "light")) do
        Hud.Text(line, x + pad, cy, 18 * s, white(0.82 * a), "light")
        cy = cy + 26 * s
    end
    cy = cy + 18 * s

    if max <= 0 then
        Hud.Rect(x + pad, cy, inner, 54 * s, tint(bad, 0.12 * a), 4 * s)
        Hud.Text("NOT AVAILABLE FOR THIS CAR", x + pad + 16 * s, cy + 8 * s, 15 * s, tint(bad, a), "mono", 0, 3 * s)
        Hud.Text(unavailable_reason[part.key] or "The workshop has no kit for this model", x + pad + 16 * s, cy + 28 * s, 15 * s, white(0.7 * a), "light")
        return
    end

    -- stages, stock first
    Hud.Text("STAGES", x + pad, cy, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
    cy = cy + 26 * s
    for stage = 0, max do
        local selected  = stage == cursor
        local installed = stage == cur
        local row_y     = cy
        if selected then
            Hud.Rect(x + pad - 10 * s, row_y - 6 * s, inner + 20 * s, 36 * s, amber(0.16 * a), 4 * s)
            Hud.Rect(x + pad - 10 * s, row_y - 6 * s, 3 * s, 36 * s, amber(a))
        end
        Hud.RectOutline(x + pad, row_y + 4 * s, 14 * s, 14 * s, installed and amber(a) or white(0.4 * a), 1.5 * s, 3 * s)
        if installed then
            Hud.Rect(x + pad + 3 * s, row_y + 7 * s, 8 * s, 8 * s, amber(a), 2 * s)
        end
        Hud.Text(stage_name(part, stage), x + pad + 28 * s, row_y, 19 * s, selected and white(a) or white(0.7 * a), "medium", 0, 1.5 * s)

        local status
        if installed then
            status = "FITTED"
        elseif is_owned(part, stage) then
            status = stage == 0 and "FREE" or "OWNED"
        else
            status = credits_text(part.prices[stage] or 0)
        end
        local affordable = installed or is_owned(part, stage) or wallet().credits >= (part.prices[stage] or 0)
        Hud.Text(status, x + pad + inner, row_y + 3 * s, 15 * s, installed and amber(a) or (affordable and white(0.75 * a) or tint(bad, 0.85 * a)), "mono", 1, 2 * s)
        hit.stages[stage] = { x + pad - 10 * s, row_y - 6 * s, inner + 20 * s, 36 * s }
        cy = cy + 40 * s
    end
    cy = cy + 20 * s

    cy = draw_compare(part, x + pad, cy, inner, s, a)
    cy = cy + 14 * s
    cy = draw_setting(part, x + pad, cy, inner, s, a)

    -- the button at the foot of the work order
    local by = h - 170 * s - 86 * s
    local bh = 58 * s
    local label, price_text
    if fitting then
        label = "FITTING"
    elseif cursor == cur then
        label = cursor == 0 and "STOCK FITTED" or "FITTED"
    elseif is_owned(part, cursor) then
        label = cursor == 0 and "REFIT STOCK" or "FIT"
        price_text = "OWNED"
    else
        label = "BUY & FIT"
        price_text = credits_text(part.prices[cursor] or 0)
    end
    local active = not fitting and cursor ~= cur
    Hud.Rect(x + pad, by, inner, bh, active and amber(0.95 * a) or white(0.08 * a), 4 * s)
    if fitting then
        Hud.Rect(x + pad, by, inner * smooth(fitting.time / fit_time), bh, amber(0.85 * a), 4 * s)
    end
    Hud.Text(label, x + pad + 22 * s, by + 14 * s, 24 * s, active and black(0.9 * a) or white(0.6 * a), "medium", 0, 3 * s)
    if price_text then
        Hud.Text(price_text, x + pad + inner - 22 * s, by + 18 * s, 18 * s, black(0.8 * a), "mono", 1, 2 * s)
    end
    hit.fit = active and { x + pad, by, inner, bh } or nil
end

local function drivetrain_text(st)
    return ({ [0] = "RWD", [1] = "FWD", [2] = "AWD" })[st.drivetrain] or "?"
end

local function draw_summary(w, h, s, a)
    local st = CarTuning.GetStats(car)
    if not st then
        return
    end
    local x = 80 * s
    local y = h - 128 * s
    local hp = st.power_kw * 1.341
    local cells =
    {
        { "MAX POWER",  string.format("%d HP", math.floor(hp + 0.5)),               string.format("@ %d RPM", math.floor(st.power_rpm + 0.5)) },
        { "MAX TORQUE", string.format("%d NM", math.floor(st.torque_nm + 0.5)),     string.format("@ %d RPM", math.floor(st.torque_rpm + 0.5)) },
        { "WEIGHT",     string.format("%d KG", math.floor(st.mass_kg + 0.5)),       string.format("%.2f KG/HP", st.mass_kg / math.max(hp, 1)) },
        { "LAYOUT",     drivetrain_text(st),                                         string.format("%d SPEED", st.gears) },
        { "ENGINE",     string.format("%.1f L", st.displacement_l),                  st.boost_bar > 0 and string.format("%.2f BAR BOOST", st.boost_bar) or "NATURALLY ASPIRATED" },
    }
    for i, cell in ipairs(cells) do
        local cx = x + (i - 1) * 205 * s
        Hud.Text(cell[1], cx, y, 13 * s, white(0.45 * a), "mono", 0, 4 * s)
        Hud.Text(cell[2], cx, y + 18 * s, 32 * s, white(0.95 * a), "light", 0, 0.5 * s)
        Hud.Text(cell[3], cx, y + 60 * s, 13 * s, amber(0.85 * a), "mono", 0, 2 * s)
        if i > 1 then
            Hud.Rect(cx - 18 * s, y + 4 * s, 1 * s, 70 * s, white(0.12 * a))
        end
    end
end

local function draw_hints(w, h, s, a)
    local y = h - 62 * s
    local x = w - 80 * s
    local entries =
    {
        { "ESC", "BACK" }, { "X", "STOCK" }, { "ENTER", "FIT" }, { "\xE2\x86\x90 \xE2\x86\x92", "STAGE" }, { "\xE2\x86\x91 \xE2\x86\x93", "PART" }, { "Q E", "DEPARTMENT" },
    }
    local part = current_part()
    if part.setting and stage_of(part) >= part.setting.min_stage then
        table.insert(entries, 2, { "SHIFT \xE2\x86\x90 \xE2\x86\x92", "ADJUST" })
    end
    for _, entry in ipairs(entries) do
        local lw = Hud.TextWidth(entry[2], 14 * s, "mono", 3 * s)
        x = x - lw
        Hud.Text(entry[2], x, y + 6 * s, 14 * s, white(0.65 * a), "mono", 0, 3 * s)
        local kw = Hud.TextWidth(entry[1], 15 * s, "mono")
        x = x - math.max(kw + 16 * s, 30 * s) - 10 * s
        draw_keycap(entry[1], x, y, s, a)
        x = x - 32 * s
    end
end

local function draw_toast(w, h, s, a)
    if not toast_text then
        return
    end
    local life = 2.6
    local t = toast_time
    if t > life then
        toast_text = nil
        return
    end
    local k = smooth(t / 0.2) * (1 - smooth((t - life + 0.4) / 0.4))
    local tw = Hud.TextWidth(toast_text, 16 * s, "mono", 3 * s)
    local bw = tw + 60 * s
    local x  = w * 0.5 - bw * 0.5
    local y  = h - 230 * s + (1 - k) * 16 * s
    Hud.Rect(x, y, bw, 48 * s, black(0.8 * k * a), 6 * s)
    Hud.Rect(x, y, 4 * s, 48 * s, toast_good and amber(k * a) or tint(bad, k * a))
    Hud.Text(toast_text, w * 0.5, y + 14 * s, 16 * s, white(k * a), "mono", 0.5, 3 * s)
end

--------------------------------------------------------------------------------
-- input
--------------------------------------------------------------------------------

local function pressed(...)
    for _, key in ipairs({ ... }) do
        if Input.GetKeyDown(key) then
            return true
        end
    end
    return false
end

local function select_department(index)
    department = (index - 1) % #departments + 1
    part_index = 1
    part_anim  = {}
    cursor     = clamp(stage_of(current_part()) + 1, 0, stage_max(current_part()))
end

local function select_part(index)
    local count = #departments[department].parts
    part_index  = (index - 1) % count + 1
    local part  = current_part()
    -- the cursor rests on the next stage up, the one a customer would ask about
    cursor = clamp(stage_of(part) + 1, 0, stage_max(part))
end

local function handle_input(mx, my)
    local part  = current_part()
    local shift = Input.GetKey(KeyCode.Shift_Left) or Input.GetKey(KeyCode.Shift_Right)

    if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East) then
        closing, close_time = true, 0
        return
    end
    if pressed(KeyCode.E, KeyCode.Tab, KeyCode.Right_Shoulder, KeyCode.Page_Down) then
        select_department(department + 1)
    end
    if pressed(KeyCode.Q, KeyCode.Left_Shoulder, KeyCode.Page_Up) then
        select_department(department - 1)
    end
    if pressed(KeyCode.Arrow_Down, KeyCode.S, KeyCode.DPad_Down) then
        select_part(part_index + 1)
    end
    if pressed(KeyCode.Arrow_Up, KeyCode.W, KeyCode.DPad_Up) then
        select_part(part_index - 1)
    end
    part = current_part()
    local max = stage_max(part)
    if shift and part.setting then
        if pressed(KeyCode.Arrow_Right, KeyCode.D) then adjust_setting(part, 1) end
        if pressed(KeyCode.Arrow_Left, KeyCode.A) then adjust_setting(part, -1) end
    else
        if pressed(KeyCode.Arrow_Right, KeyCode.D, KeyCode.DPad_Right) then cursor = clamp(cursor + 1, 0, max) end
        if pressed(KeyCode.Arrow_Left, KeyCode.A, KeyCode.DPad_Left) then cursor = clamp(cursor - 1, 0, max) end
    end
    if pressed(KeyCode.Enter, KeyCode.Space, KeyCode.Button_South) then
        fit(part, cursor)
    end
    if pressed(KeyCode.X, KeyCode.Delete, KeyCode.Button_West) then
        fit(part, 0)
    end

    -- the mouse hovers and clicks the same things, and only steals the selection when it moves
    local moved = math.abs(mx - mouse_last_x) + math.abs(my - mouse_last_y) > 0.5
    mouse_last_x, mouse_last_y = mx, my
    local click = pressed(KeyCode.Click_Left)
    for i, b in pairs(hit.departments or {}) do
        if inside(b, mx, my) and click and i ~= department then
            select_department(i)
            return
        end
    end
    for i, b in pairs(hit.parts or {}) do
        if inside(b, mx, my) and ((moved and i ~= part_index) or (click and i ~= part_index)) then
            select_part(i)
            return
        end
    end
    for stage, b in pairs(hit.stages or {}) do
        if inside(b, mx, my) and (moved or click) then
            cursor = stage
        end
    end
    if click and inside(hit.fit, mx, my) then
        fit(part, cursor)
    end
    if click and inside(hit.setting_minus, mx, my) then
        adjust_setting(part, -1)
    end
    if click and inside(hit.setting_plus, mx, my) then
        adjust_setting(part, 1)
    end
end

--------------------------------------------------------------------------------
-- lifecycle
--------------------------------------------------------------------------------

function shop.Open()
    car        = CarTuning.GetPlayerCar()
    car_name   = car and CarTuning.GetName(car):gsub("%s*/.*$", ""):gsub("%s*%b()", "") or ""
    time       = 0
    open_time  = 0
    closing    = false
    close_time = 0
    fitting    = nil
    toast_text = nil
    stats_cache, wrap_cache, hit = {}, {}, {}
    dept_anim, part_anim = {}, {}
    wallet()
    select_department(department)
    if car then
        create_camera()
        tick_camera()
    end
end

function shop.Close()
    release_camera()
    car = nil
end

-- returns false once the player has backed out and the fade has finished
function shop.Tick(dt)
    time       = time + dt
    open_time  = open_time + dt
    toast_time = toast_time + dt
    load_fonts()

    local w, h = Hud.GetSize()
    if w <= 0 or h <= 0 then
        return true
    end
    local s = clamp(math.min(h / 1080, w / 1920), 0.5, 2.0)

    if not car then
        Hud.Rect(0, 0, w, h, black(0.85))
        Hud.Text("TUNE SHOP", w * 0.5, h * 0.42, 64 * s, white(1), "display", 0.5)
        Hud.Text("THERE IS NO DRIVABLE CAR IN THE WORLD TO WORK ON", w * 0.5, h * 0.42 + 90 * s, 16 * s, amber(1), "mono", 0.5, 4 * s)
        Hud.Text("ESC  BACK", w * 0.5, h * 0.42 + 130 * s, 14 * s, white(0.6), "mono", 0.5, 4 * s)
        if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East, KeyCode.Enter) then
            return false
        end
        return true
    end

    tick_camera()
    if fitting then
        fitting.time = fitting.time + dt
        if fitting.time >= fit_time then
            finish_fit()
        end
    end

    local a = smooth(open_time / 0.6)
    if closing then
        close_time = close_time + dt
        a = a * (1 - smooth(close_time / 0.4))
    end

    draw_backdrop(w, h, s, a)
    draw_header(w, h, s, a)
    draw_departments(s, a, math.min(dt, 0.1))
    draw_parts(s, a, math.min(dt, 0.1))
    draw_detail(w, h, s, a)
    draw_summary(w, h, s, a)
    draw_hints(w, h, s, a)
    draw_toast(w, h, s, a)

    if closing then
        Hud.Rect(0, 0, w, h, black(smooth(close_time / 0.45)))
        return close_time < 0.45
    end
    if open_time > 0.3 then
        local mx, my = Hud.GetMousePosition()
        handle_input(mx, my)
    end
    return true
end

return shop
