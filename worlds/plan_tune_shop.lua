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

local directory = World.GetFilePath():match("^(.*[/\\])") or ""
local ui        = _G.plan_ui or dofile(directory .. "plan_ui.lua")
local owns_ui   = false

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
    tire_heat    = { label = "WORKING TEMPERATURE", better = 0, get = function(st) return st.tire_optimal_temp end, fmt = function(v) return string.format("%.0f C", v) end },
    tire_wear    = { label = "WEAR RATE", better = -1, get = function(st) return st.tire_wear_rate end, fmt = "relative" },
    boost        = { label = "PEAK BOOST",     better = 1,  get = function(st) return st.boost_bar end, fmt = function(v) return v > 0 and string.format("%.2f BAR", v) or "NONE" end },
    displacement = { label = "DISPLACEMENT",   better = 1,  get = function(st) return st.displacement_l end, fmt = function(v) return string.format("%.2f L", v) end },
    clutch       = { label = "CLUTCH CAPACITY", better = 1, get = function(st) return st.clutch_torque_nm end, fmt = function(v) return string.format("%d NM", math.floor(v + 0.5)) end },
    inertia      = { label = "ROTATING INERTIA", better = -1, get = function(st) return st.engine_inertia end, fmt = function(v) return string.format("%.3f KG M\xC2\xB2", v) end },
    shift        = { label = "SHIFT TIME",     better = -1, get = function(st) return st.shift_time end, fmt = function(v) return string.format("%d MS", math.floor(v * 1000 + 0.5)) end },
    final_drive  = { label = "FINAL DRIVE",    better = 0,  get = function(st) return st.final_drive end, fmt = function(v) return string.format("%.2f : 1", v) end },
    diff         = { label = "DIFFERENTIAL",   better = 0,  get = function(st) return st.diff_type end, fmt = function(v) return ({ [0] = "OPEN", [1] = "LOCKED", [2] = "LIMITED SLIP" })[v] or "?" end },
    grip         = { label = "ASPHALT GRIP",      better = 1,  get = function(st) return st.tire_friction end, fmt = function(v) return string.format("%.2f \xC2\xB5", v) end },
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
                stages = { "1-WAY", "1.5-WAY", "2-WAY", "FULLY ADJUSTABLE" }, prices = { 9000, 11500, 14000, 22000 },
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
                stages = { "N1 ECONOMY", "N2 COMFORT", "N3 ROAD", "S1 HARD", "S2 MEDIUM", "S3 SOFT",
                    "R1 SUPER HARD", "R2 HARD", "R3 MEDIUM", "R4 SOFT", "R5 SUPER SOFT", "DIRT", "SNOW / ICE" },
                prices = { 1500, 1800, 2200, 4000, 6000, 8000, 9500, 12000, 18000, 26000, 35000, 22000, 22000 },
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
-- The native registry supplies limits and unlocks; menus never invent simulation values.
table.insert(departments[1].parts, {
    key = "nitrous", name = "Nitrous Injection", stages = { "METERED SYSTEM" }, prices = { 12000 },
    text = "Adds oxygen and fuel under full throttle above 2,500 RPM. Hold N while driving. Higher flow delivers more power but empties the 5 kg bottle sooner. Buy a refill in SERVICE after installation.",
    rows = { "power", "torque" },
})
table.insert(departments[2].parts, {
    key = "supercharger", name = "Supercharger", stages = { "ROOTS KIT" }, prices = { 35000 },
    text = "A belt-driven compressor delivers boost from low RPM without turbo lag, but draws power from the crankshaft. Replaces a fitted turbo kit; the two kits cannot run together.",
    rows = { "power", "torque", "boost" },
})
table.insert(departments[4].parts, {
    key = "torque_controller", name = "AWD Torque Controller", stages = { "VARIABLE SPLIT" }, prices = { 15000 },
    text = "Sets how much engine torque reaches the front axle. More front drive changes traction and cornering balance; more rear drive allows greater rotation on power. Only for AWD cars.",
    rows = { "power", "torque" },
})
departments[7].parts[1].rows = { "grip", "tire_heat", "tire_wear" }
departments[7].parts[1].text = "Road, sport and racing compounds trade grip for heat requirements and wear. Softer slicks grip harder when warm but wear faster and struggle in rain or loose ground. Dirt and snow tires gain grip on those surfaces at the expense of asphalt performance."
table.insert(departments, { id = "service", label = "SERVICE", parts = {
    { key = "oil", service = true, name = "Oil & Filter Change", stages = { "FRESH OIL" }, prices = { 250 },
      text = "Oil ages with engine hours, distance, high RPM and heat. Degraded oil increases shaft friction and reduces delivered power. Fresh oil restores lubrication; it does not instantly cool a hot engine.",
      rows = { "oil_quality", "oil_temperature" } },
    { key = "chassis", service = true, name = "Chassis Restoration", stages = { "RESTORE" }, prices = { 50000 },
      text = "Distance and rough suspension travel fatigue the chassis. Lost rigidity softens the effective spring and anti-roll response. Restoration returns the original compliance without removing installed parts.",
      rows = { "chassis_quality", "distance_km" } },
    { key = "nitrous_refill", service = true, name = "Nitrous Refill", stages = { "5 KG BOTTLE" }, prices = { 1200 },
      text = "Refills the installed system to 5 kg. Gas is consumed only while injecting, under full throttle above 2,500 RPM in a forward gear. Higher flow means more power and less bottle life. Hold N to inject.",
      rows = { "nitrous_kg" } },
} })
for _, key in ipairs({ "oil_quality", "chassis_quality", "oil_temperature", "distance_km", "nitrous_kg" }) do
    local id = key
    stat_rows[id] = { label = string.upper(id:gsub("_", " ")), better = 0,
        get = function(st) return st[id] end,
        fmt = function(v)
            if id == "oil_quality" or id == "chassis_quality" then return string.format("%.1f%%", v * 100) end
            if id == "oil_temperature" then return string.format("%.1f C", v) end
            return string.format("%.2f", v)
        end }
end

local unavailable_reason =
{
    intercooler  = "Needs a combustion engine; cooling only helps with a compressor fitted",
    nitrous_refill = "Fit the nitrous injection system first",
    torque_controller = "Only for all wheel drive cars",
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
local time          = 0.0
local open_time     = 0.0
local closing       = false
local close_time    = 0.0
local department    = 1
local part_index    = 1
local cursor        = 0
local setting_index = 1
local part_anim     = {}
local stage_anim    = {}
local tab_x         = nil
local tab_w         = 0
local toast_text    = nil
local toast_time    = 0.0
local toast_good    = true
local fitting       = nil
local wrap_cache    = {}
local stats_cache   = {}
local mouse_last_x  = -1
local mouse_last_y  = -1
local hit           = {}

local clamp, lerp, smooth, ease_out, approach = ui.clamp, ui.lerp, ui.smooth, ui.ease_out, ui.approach
local white, ink, black, accent = ui.white, ui.ink, ui.black, ui.accent

local function wallet()
    if not _G.plan_garage then
        _G.plan_garage = { credits = starting_credits, owned = {} }
    end
    return _G.plan_garage
end

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local credits_text = ui.credits

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
    if part.service then return false end
    if stage == 0 then
        return true
    end
    local entry = owned_table()[part.key]
    return entry ~= nil and entry[stage] == true
end

local function stage_of(part)
    if part.service then return 0 end
    return math.max(CarTuning.GetStage(car, part.key), 0)
end

local function stage_max(part)
    if part.service then
        if part.key == "nitrous_refill" and CarTuning.GetStage(car, "nitrous") < 1 then return 0 end
        if part.key == "oil" and CarTuning.GetStageMax(car, "nitrous") < 1 then return 0 end
        return 1
    end
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
    if part.service then
        local st = CarTuning.GetStats(car)
        if st and stage > 0 then
            if part.key == "oil" then st.oil_quality = 1 end
            if part.key == "chassis" then st.chassis_quality = 1 end
            if part.key == "nitrous_refill" then st.nitrous_kg = 5 end
        end
        return st
    end
    local key = part.key .. ":" .. stage
    if not stats_cache[key] then
        stats_cache[key] = CarTuning.PreviewStats(car, part.key, stage)
    end
    return stats_cache[key]
end

local function toast(text, ok)
    toast_text, toast_time, toast_good = text, 0, ok
    if not ok then
        ui.sound("error")
    end
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
    if part.service and stage == 0 then return end
    local max = stage_max(part)
    if max <= 0 then
        toast(string.upper(unavailable_reason[part.key] or "Not available for this car"), false)
        return
    end
    if stage == stage_of(part) then
        toast(stage == 0 and "ALREADY STOCK" or "ALREADY FITTED", true)
        ui.sound("tick", 0.7, 0.8)
        return
    end
    local garage = wallet()
    if not is_owned(part, stage) then
        local price = part.prices[stage] or 0
        if garage.credits < price then
            toast("NOT ENOUGH CREDITS   \xC2\xB7   " .. credits_text(price - garage.credits) .. " SHORT", false)
            return
        end
    end
    fitting = { part = part, stage = stage, time = 0, price = is_owned(part, stage) and 0 or (part.prices[stage] or 0) }
    ui.sound("fitting")
end

local function finish_fit()
    local part, stage, price = fitting.part, fitting.stage, fitting.price
    fitting = nil
    local success
    if part.service then success = CarTuning.Service(car, part.key)
    else success = CarTuning.SetStage(car, part.key, stage) end
    if success then
        wallet().credits = wallet().credits - price
        if not part.service then
            local owned = owned_table()
            owned[part.key] = owned[part.key] or {}
            owned[part.key][stage] = true
        end
        stats_cache = {}
        if _G.plan_save_garage then _G.plan_save_garage() end
        ui.sound(price > 0 and "purchase" or "confirm")
        if part.service then
            toast("SERVICE COMPLETE", true)
        elseif stage == 0 then
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
    local target = clamp(value + direction * setting.step, setting.min, setting.max)
    if math.abs(target - value) < 1e-6 then
        ui.sound("error", 0.4)
        return
    end
    if CarTuning.SetSetting(car, setting.key, target) then
        stats_cache = {}
        if _G.plan_save_garage then _G.plan_save_garage() end
        ui.sound("tick", 0.9, 0.75 + 0.6 * clamp((target - setting.min) / (setting.max - setting.min), 0, 1))
    else
        toast("SETTING REJECTED: PARK THE CAR / KEEP GEARS IN ORDER", false)
    end
end

--------------------------------------------------------------------------------
-- drawing
--------------------------------------------------------------------------------

local function list_top(s)
    return ui.header_height * s + 112 * s
end

local function draw_backdrop(w, h, s, a)
    -- the car sits in the clear middle, the list and the work order each get their own glass
    Hud.RectGradient(0, 0, w * 0.45, h, ink(0.78 * a), ink(0.0), ink(0.0), ink(0.78 * a))
    Hud.RectGradient(0, 0, 640 * s, h, ink(0.3 * a), ink(0.0), ink(0.0), ink(0.3 * a))
    Hud.RectGradient(0, h - 260 * s, w, 260 * s, ink(0.0), ink(0.0), ink(0.7 * a), ink(0.7 * a))
end

local function draw_header(w, h, s, a)
    ui.header(w, s, a, { "ZAKYNTHOS", "HOME", "TUNE SHOP" }, { { "VEHICLE", string.upper(car_name), "plan_regular" }, { "CREDITS", credits_text(wallet().credits) } }, 1 - a)
end

-- departments run along the top like the tabs of a settings screen, a blue rule slides under the current one
local function draw_tabs(w, s, a, dt)
    local y = ui.header_height * s
    local bar_h = 56 * s
    Hud.Rect(0, y, w, bar_h, ink(0.62 * a))
    Hud.Rect(0, y + bar_h - 1 * s, w, 1 * s, white(0.08 * a))
    hit.departments = {}

    local x = ui.margin * s
    x = x + ui.key_pill("Q", x, y + bar_h * 0.5 - 12 * s, s, a * 0.9) + 26 * s
    local size = 13 * s
    for i, dept in ipairs(departments) do
        local lw = ui.label_width(dept.label, size)
        local current = i == department
        ui.label(dept.label, x, y + bar_h * 0.5 - 8 * s, size, current and white(a) or white(0.48 * a))

        local fitted = 0
        for _, part in ipairs(dept.parts) do
            if stage_of(part) > 0 then
                fitted = fitted + 1
            end
        end
        if fitted > 0 then
            Hud.Text(tostring(fitted), x + lw + 5 * s, y + bar_h * 0.5 - 15 * s, 10 * s, accent(a), "plan_strong")
        end
        if current then
            tab_x = tab_x and approach(tab_x, x, 18, dt) or x
            tab_w = tab_w > 0 and approach(tab_w, lw, 18, dt) or lw
        end
        hit.departments[i] = { x - 12 * s, y, lw + 24 * s, bar_h }
        x = x + lw + 34 * s
    end
    ui.key_pill("E", x - 8 * s, y + bar_h * 0.5 - 12 * s, s, a * 0.9)
    if tab_x then
        Hud.Rect(tab_x, y + bar_h - 3 * s, tab_w, 3 * s, accent(a))
    end
end

-- a thin meter of stages, filled blue up to the fitted stage, owned stages lit white
local function draw_meter(part, x, y, s, k, a, current, max)
    for i = 1, #part.stages do
        local col
        if i <= current then
            col = accent(a)
        elseif i <= max and is_owned(part, i) then
            col = ui.on_bar(k, 0.7 * a, 0.9)
        elseif i <= max then
            col = ui.on_bar(k, 0.3 * a, 0.9)
        else
            col = ui.on_bar(k, 0.08 * a, 0.9)
        end
        Hud.Rect(x + (i - 1) * 16 * s, y, 12 * s, 4 * s, col)
    end
end

local function draw_parts(s, a, dt)
    local dept  = departments[department]
    local x, y  = ui.margin * s, list_top(s)
    local width = 480 * s
    local bar_h = 62 * s
    hit.parts = {}
    ui.label(dept.label, x, y - 30 * s, 12 * s, white(0.9 * a))
    ui.label(#dept.parts .. " PARTS", x + ui.label_width(dept.label, 12 * s) + 16 * s, y - 30 * s, 12 * s, white(0.4 * a))
    for i, part in ipairs(dept.parts) do
        part_anim[i] = approach(part_anim[i] or 0, i == part_index and 1 or 0, 16, dt)
        local k   = part_anim[i]
        local ry  = y + (i - 1) * (bar_h + 6 * s)
        local max = stage_max(part)
        local cur = stage_of(part)
        local ra  = ease_out((open_time - 0.15 - i * 0.04) / 0.45) * a * (max > 0 and 1.0 or 0.5)
        local rx  = x + (1 - ease_out((open_time - 0.15 - i * 0.04) / 0.45)) * -18 * s

        ui.list_bar(rx, ry, width, bar_h, s, k, ra)
        Hud.Text(part.name, rx + 24 * s, ry + 11 * s, 18 * s, ui.on_bar(k, ra, 0.85), "plan_strong")

        local status
        if part.service and max > 0 then
            local st = CarTuning.GetStats(car)
            if part.key == "oil" then status = string.format("OIL %.1f%%   %.0f KM", st.oil_quality * 100, st.oil_distance_km)
            elseif part.key == "chassis" then status = string.format("RIGIDITY %.1f%%", st.chassis_quality * 100)
            else status = string.format("BOTTLE %.2f / 5 KG", st.nitrous_kg) end
        elseif max <= 0 then
            status = "NOT AVAILABLE"
        elseif cur > 0 then
            status = stage_name(part, cur)
        else
            status = "STOCK"
        end
        local status_color = (cur > 0) and accent(ra) or ui.on_bar(k, 0.8 * ra, 0.6)
        ui.label(status, rx + 24 * s, ry + 38 * s, 10 * s, status_color)
        if max > 0 and not part.service then
            draw_meter(part, rx + width - 24 * s - #part.stages * 16 * s + 4 * s, ry + bar_h * 0.5 - 2 * s, s, k, ra, cur, max)
        end
        hit.parts[i] = { rx, ry, width, bar_h }
    end
end

-- before and after for every stat the part touches, with a gauge that shows the gain in green and the loss in red
local function draw_compare(part, x, y, width, s, a)
    local now  = preview(part, stage_of(part))
    local next = preview(part, cursor)
    local base = preview(part, 0)
    if not now or not next then
        return y
    end
    ui.label("PERFORMANCE", x, y, 11 * s, white(0.45 * a))
    y = y + 26 * s
    for _, id in ipairs(part.rows) do
        local row = stat_rows[id]
        local v_now, v_next = row.get(now), row.get(next)
        local fmt = row.fmt
        if fmt == "relative" then
            local reference = math.max(row.get(base), 1e-12)
            fmt = function(v) return string.format("%d%%", math.floor(v / reference * 100 + 0.5)) end
        end
        local delta   = v_next - v_now
        local changed = math.abs(delta) > math.max(math.abs(v_now) * 0.0005, 1e-12)
        local col     = white(0.9 * a)
        if changed and row.better ~= 0 then
            col = (delta * row.better > 0) and ui.good(a) or ui.bad(a)
        elseif changed then
            col = accent(a)
        end

        ui.label(row.label, x, y + 3 * s, 10 * s, white(0.6 * a))
        local vx = x + width
        if changed then
            vx = vx - Hud.Text(fmt(v_next), vx, y - 2 * s, 17 * s, col, "plan_regular", 1) - 10 * s
            vx = vx - Hud.Text("\xE2\x80\xBA", vx, y - 4 * s, 18 * s, white(0.4 * a), "plan_regular", 1) - 10 * s
            Hud.Text(fmt(v_now), vx, y - 2 * s, 17 * s, white(0.5 * a), "plan_regular", 1)
        else
            Hud.Text(fmt(v_now), vx, y - 2 * s, 17 * s, white(0.85 * a), "plan_regular", 1)
        end

        -- the gauge, only for plain quantities where bigger and smaller mean something
        local gy = y + 24 * s
        Hud.Rect(x, gy, width, 2 * s, white(0.08 * a))
        if v_now > 0 and v_next > 0 and row.better ~= 0 then
            local scale = 0.82 / math.max(v_now, v_next)
            local low, high = math.min(v_now, v_next) * scale, math.max(v_now, v_next) * scale
            Hud.Rect(x, gy, width * low, 2 * s, white(0.55 * a))
            if changed then
                Hud.Rect(x + width * low, gy - 1 * s, width * (high - low), 4 * s, col)
            end
        end
        y = y + 44 * s
    end
    return y
end

local function draw_setting(part, x, y, width, s, a)
    hit.setting_minus, hit.setting_plus, hit.setting_next = nil, nil, nil
    if part.settings and #part.settings > 0 then
        setting_index = (setting_index - 1) % #part.settings + 1
        part.setting = part.settings[setting_index]
    end
    local setting = part.setting
    if not setting then
        return y
    end
    local unlocked = stage_of(part) >= setting.min_stage
    local value = CarTuning.GetSetting(car, setting.key)
    if setting.key == "final_drive" and value <= 0 then
        value = 1
    end
    local count = #(part.settings or { setting })
    ui.label("SETTINGS", x, y, 11 * s, white(0.45 * a))
    if count > 1 then
        ui.label(string.format("%d / %d", setting_index, count), x + ui.label_width("SETTINGS", 11 * s) + 14 * s, y, 11 * s, accent(0.8 * a))
    end
    if not unlocked then
        ui.label("FIT " .. stage_name(part, setting.min_stage) .. " TO ADJUST", x + width, y, 10 * s, white(0.35 * a), 1)
    end
    y = y + 24 * s
    local bar_a = unlocked and a or 0.35 * a
    ui.label(setting.label, x, y, 10 * s, white(0.8 * bar_a))
    Hud.Text(setting.fmt(value), x + width, y - 4 * s, 16 * s, white(0.9 * bar_a), "plan_regular", 1)
    y = y + 26 * s
    local t = clamp((value - setting.min) / (setting.max - setting.min), 0, 1)
    Hud.Rect(x, y, width, 2 * s, white(0.14 * bar_a))
    Hud.Rect(x, y, width * t, 2 * s, accent(bar_a))
    for i = 0, 10 do
        Hud.Rect(x + width * i / 10, y + 6 * s, 1 * s, (i % 5 == 0) and 6 * s or 3 * s, white(0.22 * bar_a))
    end
    Hud.Rect(x + width * t - 5 * s, y - 7 * s, 10 * s, 16 * s, unlocked and white(a) or white(0.3 * a))
    hit.setting_minus = unlocked and { x, y - 16 * s, width * 0.5, 32 * s } or nil
    hit.setting_plus  = unlocked and { x + width * 0.5, y - 16 * s, width * 0.5, 32 * s } or nil
    hit.setting_next  = { x, y - 50 * s, width, 24 * s }
    return y + 26 * s
end

local function draw_detail(w, h, s, a, dt)
    local part  = current_part()
    local width = 600 * s
    local x     = w - ui.margin * s - width + (1 - a) * 30 * s
    local y     = ui.header_height * s + 82 * s
    local pad   = 30 * s
    local inner = width - pad * 2
    local max   = stage_max(part)
    local cur   = stage_of(part)
    local bottom = h - ui.footer_height * s - 26 * s
    hit.stages, hit.fit = {}, nil

    ui.panel(x, y, width, bottom - y, s, a, 0.9)
    Hud.Rect(x, y, 64 * s, 2 * s, accent(a))

    local cy = y + 26 * s
    ui.label("WORK ORDER", x + pad, cy, 11 * s, accent(a))
    ui.label(departments[department].label, x + pad + ui.label_width("WORK ORDER", 11 * s) + 14 * s, cy, 11 * s, white(0.45 * a))
    cy = cy + 20 * s
    Hud.Text(part.name, x + pad - 2 * s, cy, 38 * s, white(a), "plan_thin", 0, 0.5 * s)
    cy = cy + 58 * s

    for _, line in ipairs(wrap(part.key, part.text, inner, 15 * s, "plan_regular")) do
        Hud.Text(line, x + pad, cy, 15 * s, white(0.72 * a), "plan_regular")
        cy = cy + 22 * s
    end
    cy = cy + 14 * s
    ui.hairline(x + pad, cy, inner, s, a)
    cy = cy + 20 * s

    if max <= 0 then
        Hud.Rect(x + pad, cy, 3 * s, 46 * s, ui.bad(a))
        ui.label("NOT AVAILABLE FOR THIS CAR", x + pad + 16 * s, cy + 4 * s, 11 * s, ui.bad(a))
        Hud.Text(unavailable_reason[part.key] or "The workshop has no kit for this model", x + pad + 16 * s, cy + 24 * s, 15 * s, white(0.7 * a), "plan_regular")
        return
    end

    -- stages, stock first, a window of five rows that scrolls with the cursor
    ui.label(part.service and "SERVICE" or "STAGE", x + pad, cy, 11 * s, white(0.45 * a))
    if max > 4 then
        ui.label(string.format("%d / %d", cursor, max), x + pad + inner, cy, 11 * s, white(0.35 * a), 1)
    end
    cy = cy + 24 * s
    local first_stage = math.max(part.service and 1 or 0, math.min(cursor - 2, max - 4))
    local last_stage  = math.min(max, first_stage + 4)
    local row_h       = 38 * s
    for stage = first_stage, last_stage do
        stage_anim[stage] = approach(stage_anim[stage] or 0, stage == cursor and 1 or 0, 18, dt)
        local k         = stage_anim[stage]
        local installed = stage == cur
        ui.list_bar(x + pad, cy, inner, row_h, s, k, a)

        local box = cy + row_h * 0.5 - 6 * s
        Hud.RectOutline(x + pad + 16 * s, box, 12 * s, 12 * s, installed and accent(a) or ui.on_bar(k, 0.5 * a, 0.9), 1.2 * s)
        if installed then
            Hud.Rect(x + pad + 19 * s, box + 3 * s, 6 * s, 6 * s, accent(a))
        end
        ui.label(stage_name(part, stage), x + pad + 42 * s, cy + row_h * 0.5 - 7 * s, 12 * s, ui.on_bar(k, a, 0.72))

        local status
        if installed then
            status = "FITTED"
        elseif is_owned(part, stage) then
            status = stage == 0 and "FREE" or "OWNED"
        else
            status = credits_text(part.prices[stage] or 0)
        end
        local affordable = installed or is_owned(part, stage) or wallet().credits >= (part.prices[stage] or 0)
        local status_color = installed and accent(a) or (affordable and ui.on_bar(k, a, 0.75) or ui.bad(a))
        Hud.Text(status, x + pad + inner - 16 * s, cy + row_h * 0.5 - 9 * s, 15 * s, status_color, "plan_regular", 1)
        hit.stages[stage] = { x + pad, cy, inner, row_h }
        cy = cy + row_h + 4 * s
    end
    cy = cy + 20 * s

    cy = draw_compare(part, x + pad, cy, inner, s, a)
    cy = cy + 6 * s
    cy = draw_setting(part, x + pad, cy, inner, s, a)

    -- the button at the foot of the work order
    local bh = 54 * s
    local by = bottom - pad - bh
    local label, price_text
    if fitting then
        label = part.service and "SERVICING" or "FITTING"
    elseif cursor == cur then
        label = cursor == 0 and "STOCK FITTED" or "FITTED"
    elseif is_owned(part, cursor) then
        label = cursor == 0 and "REFIT STOCK" or "FIT"
        price_text = "OWNED"
    else
        label = part.service and "SERVICE" or "PURCHASE & FIT"
        price_text = credits_text(part.prices[cursor] or 0)
    end
    local active = not fitting and cursor ~= cur
    if fitting then
        local p = smooth(fitting.time / fit_time)
        Hud.Rect(x + pad, by, inner, bh, accent(0.35 * a))
        Hud.Rect(x + pad, by, inner * p, bh, white(0.94 * a))
        ui.label(label, x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, p > 0.15 and ink(a) or white(a))
    elseif active then
        local pulse = 0.5 + 0.5 * math.sin(time * 3.0)
        Hud.Rect(x + pad, by, inner, bh, accent(a))
        Hud.Rect(x + pad, by, inner, 1 * s, white((0.35 + 0.25 * pulse) * a))
        ui.label(label, x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, white(a))
        if price_text then
            Hud.Text(price_text, x + pad + inner - 24 * s, by + bh * 0.5 - 11 * s, 19 * s, white(a), "plan_regular", 1)
        end
    else
        Hud.Rect(x + pad, by, inner, bh, ink(0.5 * a))
        Hud.RectOutline(x + pad, by, inner, bh, white(0.12 * a), 1 * s)
        ui.label(label, x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, white(0.45 * a))
    end
    hit.fit = active and { x + pad, by, inner, bh } or nil
end

local function drivetrain_text(st)
    return ({ [0] = "RWD", [1] = "FWD", [2] = "AWD" })[st.drivetrain] or "?"
end

-- the spec sheet of the car as it stands, along the foot of the screen
local function draw_summary(w, h, s, a)
    local st = CarTuning.GetStats(car)
    if not st then
        return
    end
    local x = ui.margin * s
    local y = h - ui.footer_height * s - 108 * s
    local hp = st.power_kw * 1.341
    local cells =
    {
        { "MAX POWER",  string.format("%d", math.floor(hp + 0.5)), "HP",               string.format("%d RPM", math.floor(st.power_rpm + 0.5)) },
        { "MAX TORQUE", string.format("%d", math.floor(st.torque_nm + 0.5)), "NM",     string.format("%d RPM", math.floor(st.torque_rpm + 0.5)) },
        { "WEIGHT",     string.format("%d", math.floor(st.mass_kg + 0.5)), "KG",       string.format("%.2f KG / HP", st.mass_kg / math.max(hp, 1)) },
        { "LAYOUT",     drivetrain_text(st), "",                                         string.format("%d SPEED", st.gears) },
        { "ENGINE",     string.format("%.1f", st.displacement_l), "L",                   st.boost_bar > 0 and string.format("%.2f BAR BOOST", st.boost_bar) or "NATURALLY ASPIRATED" },
    }
    ui.hairline(x, y - 16 * s, 5 * 190 * s - 30 * s, s, a)
    for i, cell in ipairs(cells) do
        local cx = x + (i - 1) * 190 * s
        ui.label(cell[1], cx, y, 10 * s, white(0.45 * a))
        local vw = Hud.Text(cell[2], cx - 2 * s, y + 14 * s, 36 * s, white(0.95 * a), "plan_thin")
        if cell[3] ~= "" then
            ui.label(cell[3], cx + vw + 6 * s, y + 36 * s, 11 * s, white(0.6 * a))
        end
        ui.label(cell[4], cx, y + 64 * s, 10 * s, accent(0.9 * a))
    end
end

local function draw_footer(w, h, s, a)
    local prompts =
    {
        { "Q  E", "DEPARTMENT" }, { "\xE2\x86\x91 \xE2\x86\x93", "PART" }, { "\xE2\x86\x90 \xE2\x86\x92", "STAGE" },
    }
    local part = current_part()
    if part.setting and stage_of(part) >= part.setting.min_stage then
        table.insert(prompts, { "SHIFT \xE2\x86\x90 \xE2\x86\x92", "ADJUST" })
        if part.settings and #part.settings > 1 then
            table.insert(prompts, { "Z  C", "SETTING" })
        end
    end
    table.insert(prompts, { "X", "STOCK" })
    table.insert(prompts, { "ENTER", "FIT" })
    table.insert(prompts, { "ESC", "BACK" })
    ui.footer(w, h, s, a, prompts, nil, 1 - a)
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
    ui.toast(w, h, s, a, toast_text, toast_good, k)
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

local function select_department(index, quiet)
    department = (index - 1) % #departments + 1
    part_index = 1
    setting_index = 1
    part_anim, stage_anim = {}, {}
    cursor     = clamp(stage_of(current_part()) + 1, 0, stage_max(current_part()))
    if not quiet then
        ui.sound("tab")
    end
end

local function select_part(index)
    local count = #departments[department].parts
    local next  = (index - 1) % count + 1
    if next == part_index then
        return
    end
    part_index    = next
    setting_index = 1
    stage_anim    = {}
    local part    = current_part()
    -- the cursor rests on the next stage up, the one a customer would ask about
    cursor = clamp(stage_of(part) + 1, 0, stage_max(part))
    ui.sound("move")
end

local function move_cursor(stage, max)
    stage = clamp(stage, 0, max)
    if stage ~= cursor then
        cursor = stage
        ui.sound("tick", 0.9, 0.9 + cursor * 0.05)
    end
end

local function next_setting(part, direction)
    setting_index = (setting_index - 1 + direction) % #part.settings + 1
    part.setting  = part.settings[setting_index]
    ui.sound("move", 0.8)
end

local function handle_input(mx, my)
    local part  = current_part()
    local shift = Input.GetKey(KeyCode.Shift_Left) or Input.GetKey(KeyCode.Shift_Right)

    if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East) then
        closing, close_time = true, 0
        ui.sound("back")
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
    if part.settings and #part.settings > 0 then
        if pressed(KeyCode.C) then next_setting(part, 1) end
        if pressed(KeyCode.Z) then next_setting(part, -1) end
        part.setting = part.settings[(setting_index - 1) % #part.settings + 1]
    end
    local max = stage_max(part)
    if shift and part.setting then
        if pressed(KeyCode.Arrow_Right, KeyCode.D) then adjust_setting(part, 1) end
        if pressed(KeyCode.Arrow_Left, KeyCode.A) then adjust_setting(part, -1) end
    else
        if pressed(KeyCode.Arrow_Right, KeyCode.D, KeyCode.DPad_Right) then move_cursor(cursor + 1, max) end
        if pressed(KeyCode.Arrow_Left, KeyCode.A, KeyCode.DPad_Left) then move_cursor(cursor - 1, max) end
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
        if inside(b, mx, my) and (moved or click) and i ~= part_index then
            select_part(i)
            return
        end
    end
    for stage, b in pairs(hit.stages or {}) do
        if inside(b, mx, my) and (moved or click) then
            move_cursor(stage, max)
        end
    end
    if click and inside(hit.fit, mx, my) then
        fit(part, cursor)
    end
    if click and inside(hit.setting_next, mx, my) and part.settings and #part.settings > 0 then
        next_setting(part, 1)
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
    tab_x, tab_w = nil, 0
    stats_cache, wrap_cache, hit = {}, {}, {}
    part_anim, stage_anim = {}, {}
    if not ui.ready then
        ui.init()
        owns_ui = true
    end
    wallet()
    if car then
        local settings = CarTuning.GetSettings(car)
        for _, dept in ipairs(departments) do
            for _, part in ipairs(dept.parts) do
                part.settings, part.setting = {}, nil
                for _, setting in ipairs(settings) do
                    if setting.part == part.key then
                        local key = setting.key
                        if key:find("camber") or key:find("toe") then
                            setting.label = setting.label:gsub(" %(rad%)", "")
                            setting.fmt = function(v) return string.format("%.2f DEG", v * 180 / math.pi) end
                        elseif key:find("ride_") then
                            setting.label = setting.label:gsub(" %(m%)", "")
                            setting.fmt = function(v) return string.format("%+.0f MM", v * 1000) end
                        elseif key == "torque_split" or key == "brake_bias" or key == "lsd_accel" or key == "lsd_decel" then
                            setting.fmt = function(v) return string.format("%.0f%%", v * 100) end
                        else
                            setting.fmt = function(v) return string.format("%.3f", v) end
                        end
                        table.insert(part.settings, setting)
                    end
                end
                part.setting = part.settings[1]
            end
        end
    end
    if car then
        select_department(department, true)
        create_camera()
        tick_camera()
    end
end

function shop.Close()
    if _G.plan_save_garage then _G.plan_save_garage() end
    release_camera()
    if owns_ui then
        ui.release()
        owns_ui = false
    end
    car = nil
end

-- returns false once the player has backed out and the fade has finished
function shop.Tick(dt)
    time       = time + dt
    open_time  = open_time + dt
    toast_time = toast_time + dt
    ui.load_fonts()

    local w, h = Hud.GetSize()
    if w <= 0 or h <= 0 then
        return true
    end
    local s = clamp(math.min(h / 1080, w / 1920), 0.5, 2.0)

    if not car then
        Hud.Rect(0, 0, w, h, ink(0.92))
        ui.header(w, s, 1, { "ZAKYNTHOS", "HOME", "TUNE SHOP" }, {})
        Hud.Text("NO VEHICLE", w * 0.5, h * 0.42, 64 * s, white(1), "plan_thin", 0.5, 12 * s)
        ui.label("THERE IS NO DRIVABLE CAR IN THE WORLD TO WORK ON", w * 0.5, h * 0.42 + 96 * s, 12 * s, accent(1), 0.5)
        ui.footer(w, h, s, 1, { { "ESC", "BACK" } })
        if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East, KeyCode.Enter) then
            ui.sound("back")
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

    local a = smooth(open_time / 0.5)
    if closing then
        close_time = close_time + dt
        a = a * (1 - smooth(close_time / 0.35))
    end
    local adt = math.min(dt, 0.1)

    draw_backdrop(w, h, s, a)
    draw_tabs(w, s, a, adt)
    draw_parts(s, a, adt)
    draw_detail(w, h, s, a, adt)
    draw_summary(w, h, s, a)
    draw_header(w, h, s, a)
    draw_footer(w, h, s, a)
    draw_toast(w, h, s, a)

    if closing then
        Hud.Rect(0, 0, w, h, ink(smooth(close_time / 0.45)))
        return close_time < 0.45
    end
    if open_time > 0.3 then
        local mx, my = Hud.GetMousePosition()
        handle_input(mx, my)
    end
    return true
end

return shop
