--------------------------------------------------------------------------------
-- plan gt auto
--
-- zante auto, the island's service shop, laid out like the gt auto of the old
-- garage racers: the light jobs a car needs between races, each priced on one
-- line, a work order on the right with the condition before and after, and a
-- strip along the foot with the state of the whole car.
--
-- the menu opens when the player drives onto the shop's forecourt
-- (plan_gt_auto_entrance.lua), with the car held over the inspection pit. the
-- car wash is a real run through the tunnel next door: the car rolls into the
-- wash bay, the roll over gantry passes it twice and the dirt comes off at the
-- turnaround, then the car is brought back to the pit.
--
-- the bays are authored in the shop's local space (gt_auto_hub, built by
-- binaries/project/plan_resources/gt_auto), so they follow the building.
--------------------------------------------------------------------------------

local service_shop = {}

local directory = World.GetFilePath():match("^(.*[/\\])") or ""
local ui        = _G.plan_ui or dofile(directory .. "plan_ui.lua")
local owns_ui   = false

local starting_credits = 500000
local service_time     = 1.2

-- shop local space: x across the front (reception at +x), y up from the floor, z toward the road
local pit_spot     = { -5.7, 0.0, -1.2 }
local wash_x       = -13.1
local wash_spot    = { wash_x, 0.0, -0.3 }
local wash_eye     = { wash_x + 1.6, 2.6, 7.8 }
local wash_look    = { wash_x - 0.2, 0.7, -1.5 }
-- the gantry parks in front of the dryer and runs to the spray arch and back, the brushes ride with it
local gantry_rest  = -4.6
local gantry_front = 4.6
local brush_offset = 1.95
local brush_top_y  = 2.05
local wash_fade    = 0.45
local wash_run     = 9.0

local services =
{
    {
        key = "wash", name = "Car Wash", price = 50, gauge = "CLEANLINESS",
        text = "A run through the roll over tunnel: pre-wash foam, two passes of the soft foam brushes, a rinse and the blow dryer. Road dust, mud and dirt come off the paint and glass. Scratches and chips are bodywork, not dirt, and stay.",
    },
    {
        key = "oil", name = "Oil Change", price = 250, gauge = "OIL CONDITION",
        text = "Oil ages with engine hours, distance, high RPM and heat. Worn oil adds friction inside the engine and takes power away from the wheels. Fresh oil and a new filter bring the lubrication back to new; it does not cool a hot engine on the spot.",
    },
    {
        key = "tyres", name = "New Tyres", price = 800, gauge = "TREAD",
        text = "A new set of the compound already fitted, mounted and balanced on the changer in bay 02. Worn tyres lose grip and overheat sooner. The compound itself is chosen at Zante Tuning.",
    },
    {
        key = "nitrous_refill", name = "Nitrous Refill", price = 1200, gauge = "BOTTLE",
        text = "Refills the installed system to a full 5 kg bottle. Gas is used only while injecting, at full throttle above 2,500 RPM in a forward gear. Hold N to inject.",
    },
    {
        key = "chassis", name = "Chassis Restoration", price = 50000, gauge = "RIGIDITY",
        text = "Distance and rough roads fatigue the body shell. Lost rigidity softens the effective springs and anti-roll bars. A restoration on the jig brings the original stiffness back without touching any fitted parts.",
    },
}

--------------------------------------------------------------------------------
-- state
--------------------------------------------------------------------------------

local crumbs        = { "ZAKYNTHOS", "ZANTE AUTO", "SERVICE" }
local hub           = nil
local car           = nil
local car_name      = ""
local camera_entity = nil
local camera_mode   = "pit"
local gantry        = {}
local brush_angle   = 0.0
local time          = 0.0
local open_time     = 0.0
local closing       = false
local close_time    = 0.0
local index         = 1
local row_anim      = {}
local toast_text    = nil
local toast_time    = 0.0
local toast_good    = true
local job           = nil
local wrap_cache    = {}
local mouse_last_x  = -1
local mouse_last_y  = -1
local hit           = {}

local clamp, lerp, smooth, ease_out, approach = ui.clamp, ui.lerp, ui.smooth, ui.ease_out, ui.approach
local white, ink, black, accent = ui.white, ui.ink, ui.black, ui.accent
local credits_text = ui.credits

local function wallet()
    if not _G.plan_garage then
        _G.plan_garage = { credits = starting_credits, owned = {} }
    end
    return _G.plan_garage
end

--------------------------------------------------------------------------------
-- the car's condition, one reading per service
--------------------------------------------------------------------------------

-- level is 0 worn out to 1 as new, nil when the job cannot be done on this car, with the reason
local function reading(service)
    local st = CarTuning.GetStats(car)
    if not st then
        return nil, "This car has no simulation to service"
    end
    local key = service.key
    if key == "wash" then
        local dirt = CarTuning.GetDirt()
        return 1 - dirt, string.format("%d%% DIRT", math.floor(dirt * 100 + 0.5))
    elseif key == "oil" then
        if st.electric then
            return nil, "Electric cars have no engine oil"
        end
        return st.oil_quality, string.format("%.0f KM SINCE THE LAST CHANGE", st.oil_distance_km)
    elseif key == "tyres" then
        local wear = CarTuning.GetTireWear(car)
        if wear < 0 then
            return nil, "The tyres cannot be reached on this car"
        end
        return 1 - wear, string.format("%d%% WORN", math.floor(wear * 100 + 0.5))
    elseif key == "nitrous_refill" then
        if CarTuning.GetStage(car, "nitrous") < 1 then
            return nil, "Fit nitrous injection at Zante Tuning first"
        end
        return st.nitrous_kg / 5, string.format("%.2f / 5.00 KG", st.nitrous_kg)
    elseif key == "chassis" then
        return st.chassis_quality, string.format("%.0f KM ON THE ODOMETER", st.distance_km)
    end
end

local function needed(level)
    return level and level < 0.995
end

local function perform(service)
    local key = service.key
    if key == "wash" then
        return CarTuning.Wash()
    elseif key == "tyres" then
        return CarTuning.ReplaceTires(car)
    end
    return CarTuning.Service(car, key)
end

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local function to_world(p)
    return hub:GetPosition() + hub:GetRight() * p[1] + hub:GetUp() * p[2] + hub:GetForward() * p[3]
end

local function park(spot)
    -- nose to the open door, so the player drives straight out
    CarTuning.PlaceAt(car, to_world(spot), Quaternion.FromLookRotation(hub:GetForward(), Vector3(0, 1, 0)))
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

local function toast(text, ok)
    toast_text, toast_time, toast_good = text, 0, ok
    if not ok then
        ui.sound("error")
    end
end

local function percent(level)
    return string.format("%d%%", math.floor(clamp(level, 0, 1) * 100 + 0.5))
end

-- red when it needs doing, amber on the way, blue when it is fine
local function level_color(level, a)
    if level < 0.4 then
        return ui.bad(a)
    elseif level < 0.75 then
        return Vector4(0.95, 0.66, 0.2, a)
    end
    return accent(a)
end

--------------------------------------------------------------------------------
-- the wash tunnel
--------------------------------------------------------------------------------

local function find_gantry()
    gantry = {}
    for _, name in ipairs({ "wash_gantry", "wash_brush_l", "wash_brush_r", "wash_brush_top" }) do
        gantry[name] = hub:GetChildByName("gt_auto_" .. name)
    end
end

local function set_gantry(z, angle)
    if gantry.wash_gantry then
        gantry.wash_gantry:SetPositionLocal(Vector3(wash_x, 0, z))
    end
    if gantry.wash_brush_l then
        gantry.wash_brush_l:SetPositionLocal(Vector3(wash_x - brush_offset, 0, z))
        gantry.wash_brush_l:SetRotationLocal(Quaternion.FromEulerAngles(0, angle, 0))
    end
    if gantry.wash_brush_r then
        gantry.wash_brush_r:SetPositionLocal(Vector3(wash_x + brush_offset, 0, z))
        gantry.wash_brush_r:SetRotationLocal(Quaternion.FromEulerAngles(0, -angle, 0))
    end
    if gantry.wash_brush_top then
        gantry.wash_brush_top:SetPositionLocal(Vector3(wash_x, brush_top_y, z))
        gantry.wash_brush_top:SetRotationLocal(Quaternion.FromEulerAngles(angle, 0, 0))
    end
end

-- 0 to 1 over the run: in toward the spray arch, back to the dryer
local function gantry_z(p)
    local t = p < 0.5 and smooth(p * 2) or 1 - smooth((p - 0.5) * 2)
    return lerp(gantry_rest, gantry_front, t)
end

local function wash_stage(p)
    if p < 0.12 then return "PRE-WASH FOAM" end
    if p < 0.5 then return "BRUSH  \xC2\xB7  FIRST PASS" end
    if p < 0.88 then return "RINSE  \xC2\xB7  SECOND PASS" end
    return "DRYING"
end

--------------------------------------------------------------------------------
-- camera: a slow sweep around the car over the pit, a fixed view down the tunnel for the wash
--------------------------------------------------------------------------------

local function create_camera()
    camera_entity = World.CreateEntity()
    camera_entity:SetName("gt_auto_camera")
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
    local up = Vector3(0, 1, 0)
    local eye, target
    if camera_mode == "wash" then
        eye    = to_world(wash_eye) + up * math.sin(time * 0.4) * 0.04
        target = to_world(wash_look)
    else
        -- the pit bay is only seven metres wide, so the sweep stays in front of the car between the columns
        local center  = car:GetPosition()
        local forward = car:GetForward()
        local right   = car:GetRight()
        local angle   = math.rad(math.sin(time * 0.11) * 24)
        local radius  = 7.2
        eye    = center + forward * (math.cos(angle) * radius) + right * (math.sin(angle) * radius) + up * (1.9 + math.sin(time * 0.17) * 0.1)
        target = center + up * 0.2
    end
    camera_entity:SetPosition(eye)
    camera_entity:SetRotation(Quaternion.FromLookRotation(target - eye, up))
end

--------------------------------------------------------------------------------
-- actions
--------------------------------------------------------------------------------

local function order(service)
    if job or not car then
        return
    end
    local level, detail = reading(service)
    if not level then
        toast(string.upper(detail), false)
        return
    end
    if not needed(level) then
        toast(service.key == "wash" and "THE CAR IS ALREADY CLEAN" or "NOTHING TO DO  \xC2\xB7  ALREADY AS NEW", true)
        ui.sound("tick", 0.7, 0.8)
        return
    end
    local garage = wallet()
    if garage.credits < service.price then
        toast("NOT ENOUGH CREDITS   \xC2\xB7   " .. credits_text(service.price - garage.credits) .. " SHORT", false)
        return
    end
    job = { service = service, time = 0, done = false, wash = service.key == "wash" }
    ui.sound(job.wash and "transition" or "fitting", 0.8)
end

local function complete(service)
    if perform(service) then
        wallet().credits = wallet().credits - service.price
        if _G.plan_save_garage then _G.plan_save_garage() end
        ui.sound("purchase")
        return true
    end
    toast("THE WORKSHOP COULD NOT DO THAT", false)
    return false
end

-- runs the order, returns the black overlay strength for the wash's cuts
local function tick_job(dt)
    if not job then
        return 0
    end
    job.time = job.time + dt
    local t = job.time
    if not job.wash then
        if t >= service_time then
            if complete(job.service) then
                toast(string.upper(job.service.name) .. "  \xC2\xB7  DONE", true)
            end
            job = nil
        end
        return 0
    end

    local run_start = wash_fade * 2
    local run_end   = run_start + wash_run
    if t >= wash_fade and not job.in_bay then
        job.in_bay = true
        park(wash_spot)
        camera_mode = "wash"
    end
    if t >= run_start and t < run_end then
        local p = (t - run_start) / wash_run
        brush_angle = brush_angle + dt * 400
        set_gantry(gantry_z(p), brush_angle)
        if p >= 0.5 and not job.done then
            job.done = true
            job.paid = complete(job.service)
        end
    end
    if t >= run_end + wash_fade and not job.back then
        job.back = true
        set_gantry(gantry_rest, brush_angle)
        park(pit_spot)
        camera_mode = "pit"
    end
    if t >= run_end + wash_fade * 2 then
        if job.paid then
            toast("CAR WASH  \xC2\xB7  DONE", true)
        end
        job = nil
        return 0
    end
    if t < wash_fade then return smooth(t / wash_fade) end
    if t < run_start then return 1 - smooth((t - wash_fade) / wash_fade) end
    if t < run_end then return 0 end
    if t < run_end + wash_fade then return smooth((t - run_end) / wash_fade) end
    return 1 - smooth((t - run_end - wash_fade) / wash_fade)
end

local function washing()
    return job and job.wash and job.in_bay and not job.back
end

--------------------------------------------------------------------------------
-- drawing
--------------------------------------------------------------------------------

local function list_top(s)
    return ui.header_height * s + 112 * s
end

local function draw_backdrop(w, h, s, a)
    Hud.RectGradient(0, 0, w * 0.45, h, ink(0.78 * a), ink(0.0), ink(0.0), ink(0.78 * a))
    Hud.RectGradient(0, 0, 640 * s, h, ink(0.3 * a), ink(0.0), ink(0.0), ink(0.3 * a))
    Hud.RectGradient(0, h - 260 * s, w, 260 * s, ink(0.0), ink(0.0), ink(0.7 * a), ink(0.7 * a))
end

local function draw_header(w, h, s, a)
    ui.header(w, s, a, crumbs, { { "VEHICLE", string.upper(car_name), "plan_regular" }, { "CREDITS", credits_text(wallet().credits) } }, 1 - a)
end

-- every job on one line with its price and a gauge of what it would put right, like the board on the shop wall
local function draw_list(s, a, dt)
    local x, y  = ui.margin * s, list_top(s)
    local width = 480 * s
    local bar_h = 70 * s
    hit.rows = {}
    ui.label("SERVICE MENU", x, y - 30 * s, 12 * s, white(0.9 * a))
    ui.label(#services .. " JOBS", x + ui.label_width("SERVICE MENU", 12 * s) + 16 * s, y - 30 * s, 12 * s, white(0.4 * a))
    for i, service in ipairs(services) do
        row_anim[i] = approach(row_anim[i] or 0, i == index and 1 or 0, 16, dt)
        local k = row_anim[i]
        local level, detail = reading(service)
        local reveal = ease_out((open_time - 0.15 - i * 0.04) / 0.45)
        local ra = reveal * a * (level and 1.0 or 0.5)
        local rx = x + (1 - reveal) * -18 * s
        local ry = y + (i - 1) * (bar_h + 6 * s)

        ui.list_bar(rx, ry, width, bar_h, s, k, ra)
        Hud.Text(service.name, rx + 24 * s, ry + 12 * s, 18 * s, ui.on_bar(k, ra, 0.85), "plan_strong")
        Hud.Text(credits_text(service.price), rx + width - 24 * s, ry + 13 * s, 15 * s, ui.on_bar(k, ra, 0.75), "plan_regular", 1)
        if level then
            local gx, gy, gw = rx + 24 * s, ry + 46 * s, width - 48 * s - 64 * s
            Hud.Rect(gx, gy, gw, 4 * s, ui.on_bar(k, 0.14 * ra, 0.9))
            Hud.Rect(gx, gy, gw * clamp(level, 0, 1), 4 * s, level_color(level, ra))
            Hud.Text(percent(level), rx + width - 24 * s, gy - 8 * s, 13 * s, needed(level) and level_color(level, ra) or ui.on_bar(k, 0.7 * ra, 0.6), "plan_regular", 1)
        else
            ui.label("NOT AVAILABLE", rx + 24 * s, ry + 44 * s, 10 * s, ui.on_bar(k, 0.8 * ra, 0.6))
        end
        hit.rows[i] = { rx, ry, width, bar_h }
    end
end

local function draw_order(w, h, s, a, dt)
    local service = services[index]
    local width = 600 * s
    local x     = w - ui.margin * s - width + (1 - a) * 30 * s
    local y     = ui.header_height * s + 82 * s
    local pad   = 30 * s
    local inner = width - pad * 2
    local bottom = h - ui.footer_height * s - 26 * s
    local level, detail = reading(service)
    hit.order = nil

    ui.panel(x, y, width, bottom - y, s, a, 0.9)
    Hud.Rect(x, y, 64 * s, 2 * s, accent(a))

    local cy = y + 26 * s
    ui.label("WORK ORDER", x + pad, cy, 11 * s, accent(a))
    ui.label(string.format("JOB %d / %d", index, #services), x + pad + ui.label_width("WORK ORDER", 11 * s) + 14 * s, cy, 11 * s, white(0.45 * a))
    cy = cy + 20 * s
    Hud.Text(service.name, x + pad - 2 * s, cy, 38 * s, white(a), "plan_thin", 0, 0.5 * s)
    cy = cy + 58 * s
    for _, line in ipairs(wrap(service.key, service.text, inner, 15 * s, "plan_regular")) do
        Hud.Text(line, x + pad, cy, 15 * s, white(0.72 * a), "plan_regular")
        cy = cy + 22 * s
    end
    cy = cy + 14 * s
    ui.hairline(x + pad, cy, inner, s, a)
    cy = cy + 20 * s

    if not level then
        Hud.Rect(x + pad, cy, 3 * s, 46 * s, ui.bad(a))
        ui.label("NOT AVAILABLE FOR THIS CAR", x + pad + 16 * s, cy + 4 * s, 11 * s, ui.bad(a))
        Hud.Text(detail, x + pad + 16 * s, cy + 24 * s, 15 * s, white(0.7 * a), "plan_regular")
        return
    end

    -- the condition now and after the job, the gauge fills the gap in blue
    ui.label(service.gauge, x + pad, cy, 11 * s, white(0.45 * a))
    local after = needed(level) and 1 or level
    local vx = x + pad + inner
    if needed(level) then
        vx = vx - Hud.Text(percent(after), vx, cy - 8 * s, 26 * s, ui.good(a), "plan_regular", 1) - 10 * s
        vx = vx - Hud.Text("\xE2\x80\xBA", vx, cy - 8 * s, 26 * s, white(0.4 * a), "plan_regular", 1) - 10 * s
    end
    Hud.Text(percent(level), vx, cy - 8 * s, 26 * s, needed(level) and level_color(level, a) or white(0.85 * a), "plan_regular", 1)
    cy = cy + 34 * s
    Hud.Rect(x + pad, cy, inner, 6 * s, white(0.08 * a))
    Hud.Rect(x + pad, cy, inner * clamp(level, 0, 1), 6 * s, level_color(level, a))
    if needed(level) then
        local pulse = 0.55 + 0.25 * math.sin(time * 3.0)
        Hud.Rect(x + pad + inner * level, cy, inner * (1 - level), 6 * s, ui.good(pulse * a))
    end
    for i = 0, 10 do
        Hud.Rect(x + pad + inner * i / 10, cy + 10 * s, 1 * s, (i % 5 == 0) and 6 * s or 3 * s, white(0.22 * a))
    end
    cy = cy + 30 * s
    ui.label(detail, x + pad, cy, 10 * s, white(0.6 * a))

    -- the button at the foot of the work order
    local bh = 54 * s
    local by = bottom - pad - bh
    if job and not job.wash then
        local p = smooth(job.time / service_time)
        Hud.Rect(x + pad, by, inner, bh, accent(0.35 * a))
        Hud.Rect(x + pad, by, inner * p, bh, white(0.94 * a))
        ui.label("IN THE WORKSHOP", x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, p > 0.15 and ink(a) or white(a))
    elseif needed(level) then
        local affordable = wallet().credits >= service.price
        local pulse = 0.5 + 0.5 * math.sin(time * 3.0)
        Hud.Rect(x + pad, by, inner, bh, affordable and accent(a) or ink(0.6 * a))
        Hud.Rect(x + pad, by, inner, 1 * s, white((0.35 + 0.25 * pulse) * a))
        ui.label(service.key == "wash" and "WASH THE CAR" or "SERVICE", x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, white(a))
        Hud.Text(credits_text(service.price), x + pad + inner - 24 * s, by + bh * 0.5 - 11 * s, 19 * s, affordable and white(a) or ui.bad(a), "plan_regular", 1)
        hit.order = { x + pad, by, inner, bh }
    else
        Hud.Rect(x + pad, by, inner, bh, ink(0.5 * a))
        Hud.RectOutline(x + pad, by, inner, bh, white(0.12 * a), 1 * s)
        ui.label(service.key == "wash" and "ALREADY CLEAN" or "AS NEW", x + pad + 24 * s, by + bh * 0.5 - 8 * s, 14 * s, white(0.45 * a))
    end
end

-- the state of the whole car along the foot of the screen
local function draw_condition(w, h, s, a)
    local st = CarTuning.GetStats(car)
    if not st then
        return
    end
    local x = ui.margin * s
    local y = h - ui.footer_height * s - 108 * s
    local cells = {}
    for _, key in ipairs({ "wash", "oil", "tyres", "chassis" }) do
        for _, service in ipairs(services) do
            if service.key == key then
                local level = reading(service)
                table.insert(cells, { service.gauge, level })
            end
        end
    end
    table.insert(cells, { "ODOMETER", nil, string.format("%.0f", st.distance_km), "KM" })
    ui.hairline(x, y - 16 * s, #cells * 190 * s - 30 * s, s, a)
    for i, cell in ipairs(cells) do
        local cx = x + (i - 1) * 190 * s
        ui.label(cell[1], cx, y, 10 * s, white(0.45 * a))
        if cell[3] then
            local vw = Hud.Text(cell[3], cx - 2 * s, y + 14 * s, 36 * s, white(0.95 * a), "plan_thin")
            ui.label(cell[4], cx + vw + 6 * s, y + 36 * s, 11 * s, white(0.6 * a))
        elseif cell[2] then
            local vw = Hud.Text(string.format("%d", math.floor(clamp(cell[2], 0, 1) * 100 + 0.5)), cx - 2 * s, y + 14 * s, 36 * s, white(0.95 * a), "plan_thin")
            ui.label("%", cx + vw + 6 * s, y + 36 * s, 11 * s, white(0.6 * a))
            Hud.Rect(cx, y + 66 * s, 150 * s, 2 * s, white(0.1 * a))
            Hud.Rect(cx, y + 66 * s, 150 * s * clamp(cell[2], 0, 1), 2 * s, level_color(cell[2], a))
        else
            Hud.Text("\xE2\x80\x94", cx - 2 * s, y + 14 * s, 36 * s, white(0.4 * a), "plan_thin")
        end
    end
end

local function draw_footer(w, h, s, a)
    ui.footer(w, h, s, a, { { "\xE2\x86\x91 \xE2\x86\x93", "JOB" }, { "ENTER", "ORDER" }, { "ESC", "BACK" } }, nil, 1 - a)
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

-- the tunnel run: a caption low on the left with the programme step and a progress rule
local function draw_wash(w, h, s)
    local run_start = wash_fade * 2
    local p = clamp((job.time - run_start) / wash_run, 0, 1)
    local a = smooth((job.time - wash_fade) / 0.4)
    Hud.RectGradient(0, h - 220 * s, w, 220 * s, ink(0.0), ink(0.0), ink(0.75 * a), ink(0.75 * a))
    local x, y = ui.margin * s, h - 150 * s
    Hud.Rect(x, y, 4 * s, 78 * s, accent(a))
    ui.label("ZANTE AUTO  \xC2\xB7  CAR WASH", x + 24 * s, y, 12 * s, accent(a))
    Hud.Text(wash_stage(p), x + 22 * s, y + 20 * s, 34 * s, white(a), "plan_thin", 0, 0.5 * s)
    Hud.Rect(x + 24 * s, y + 70 * s, 420 * s, 2 * s, white(0.14 * a))
    Hud.Rect(x + 24 * s, y + 70 * s, 420 * s * p, 2 * s, accent(a))
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

local function select_job(i)
    local next = (i - 1) % #services + 1
    if next ~= index then
        index = next
        ui.sound("move")
    end
end

local function handle_input(mx, my)
    if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East) then
        closing, close_time = true, 0
        ui.sound("back")
        return
    end
    if pressed(KeyCode.Arrow_Down, KeyCode.S, KeyCode.DPad_Down) then
        select_job(index + 1)
    end
    if pressed(KeyCode.Arrow_Up, KeyCode.W, KeyCode.DPad_Up) then
        select_job(index - 1)
    end
    if pressed(KeyCode.Enter, KeyCode.Space, KeyCode.Button_South) then
        order(services[index])
    end

    -- the mouse hovers and clicks the same things, and only steals the selection when it moves
    local moved = math.abs(mx - mouse_last_x) + math.abs(my - mouse_last_y) > 0.5
    mouse_last_x, mouse_last_y = mx, my
    local click = pressed(KeyCode.Click_Left)
    for i, b in pairs(hit.rows or {}) do
        if inside(b, mx, my) and (moved or click) then
            select_job(i)
        end
    end
    if click and inside(hit.order, mx, my) then
        order(services[index])
    end
end

--------------------------------------------------------------------------------
-- lifecycle
--------------------------------------------------------------------------------

-- options.hub is the shop's gt_auto_hub entity, the car is expected held over the pit
function service_shop.Open(options)
    hub        = options.hub
    car        = CarTuning.GetPlayerCar()
    car_name   = car and CarTuning.GetName(car):gsub("%s*/.*$", ""):gsub("%s*%b()", "") or ""
    time, open_time = 0, 0
    closing, close_time = false, 0
    job, toast_text = nil, nil
    camera_mode = "pit"
    wrap_cache, hit, row_anim = {}, {}, {}
    if not ui.ready then
        ui.init()
        owns_ui = true
    end
    wallet()
    find_gantry()
    set_gantry(gantry_rest, brush_angle)
    if car then
        create_camera()
        tick_camera()
    end
end

function service_shop.Close()
    if job and job.wash and car then
        park(pit_spot)
    end
    job = nil
    set_gantry(gantry_rest, brush_angle)
    if _G.plan_save_garage then _G.plan_save_garage() end
    release_camera()
    if owns_ui then
        ui.release()
        owns_ui = false
    end
    car = nil
end

-- returns false once the player has backed out and the fade has finished
function service_shop.Tick(dt)
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
        ui.header(w, s, 1, crumbs, {})
        Hud.Text("NO VEHICLE", w * 0.5, h * 0.42, 64 * s, white(1), "plan_thin", 0.5, 12 * s)
        ui.label("THERE IS NO DRIVABLE CAR IN THE WORLD TO SERVICE", w * 0.5, h * 0.42 + 96 * s, 12 * s, accent(1), 0.5)
        ui.footer(w, h, s, 1, { { "ESC", "BACK" } })
        if pressed(KeyCode.Esc, KeyCode.Backspace, KeyCode.Button_East, KeyCode.Enter) then
            ui.sound("back")
            return false
        end
        return true
    end

    local cut = tick_job(math.min(dt, 0.1))
    tick_camera()

    local a = smooth(open_time / 0.5)
    if closing then
        close_time = close_time + dt
        a = a * (1 - smooth(close_time / 0.35))
    end
    local adt = math.min(dt, 0.1)

    if washing() then
        draw_wash(w, h, s)
    elseif not (job and job.wash) or cut > 0 then
        local ma = (job and job.wash) and a * (1 - cut) or a
        draw_backdrop(w, h, s, ma)
        draw_list(s, ma, adt)
        draw_order(w, h, s, ma, adt)
        draw_condition(w, h, s, ma)
        draw_header(w, h, s, ma)
        draw_footer(w, h, s, ma)
    end
    draw_toast(w, h, s, a)
    if cut > 0 then
        Hud.Rect(0, 0, w, h, black(cut))
    end

    if closing then
        Hud.Rect(0, 0, w, h, ink(smooth(close_time / 0.45)))
        return close_time < 0.45
    end
    if open_time > 0.3 and not job then
        local mx, my = Hud.GetMousePosition()
        handle_input(mx, my)
    end
    return true
end

return service_shop
