--------------------------------------------------------------------------------
-- plan gt auto entrance
--
-- the way into zante auto, the service shop between the garage and zante tuning.
-- roll the car onto the forecourt in front of the wash tunnel or the pit bay and
-- a prompt offers the service menu; taking it dips to black, rolls the car over
-- the inspection pit, holds it there and opens the menu (plan_gt_auto.lua).
-- backing out of the menu lifts the hold and leaves the car over the pit, nose to
-- the open door.
--
-- the forecourt is authored in the shop's local space (gt_auto_hub, built by
-- binaries/project/plan_resources/gt_auto), so it follows the building.
--------------------------------------------------------------------------------

local entrance = {}

local directory = World.GetFilePath():match("^(.*[/\\])") or ""
local ui        = _G.plan_ui or dofile(directory .. "plan_ui.lua")

local clamp, smooth, approach = ui.clamp, ui.smooth, ui.approach
local white, ink, black, accent = ui.white, ui.ink, ui.black, ui.accent

-- shop local space: x across the front (reception at +x), y up from the floor, z toward the road
local forecourt = { x0 = -17.0, x1 = 4.5, z0 = -8.8, z1 = 17.8 } -- the wash and pit lanes and the two bays behind them
local lot       = { x0 = -22.0, x1 = 22.0, z0 = -12.5, z1 = 20.2 }
local pit_spot  = { -5.7, 0.0, -1.2 }
local max_speed = 25.0 / 3.6
local fade_in   = 0.45
local fade_out  = 0.7

local hub        = nil
local shop       = nil
local owns_ui    = false
local state      = "idle"
local state_time = 0.0
local prompt     = 0.0
local hint       = 0.0
local speed      = 0.0
local last_pos   = nil
local car        = nil

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local function to_world(p)
    return hub:GetPosition() + hub:GetRight() * p[1] + hub:GetUp() * p[2] + hub:GetForward() * p[3]
end

local function to_local(position)
    local d = position - hub:GetPosition()
    local r, f = hub:GetRight(), hub:GetForward()
    return d.x * r.x + d.y * r.y + d.z * r.z, d.x * f.x + d.y * f.y + d.z * f.z
end

local function inside(area, position)
    local x, z = to_local(position)
    return x >= area.x0 and x <= area.x1 and z >= area.z0 and z <= area.z1
end

local function pressed(...)
    for _, key in ipairs({ ... }) do
        if Input.GetKeyDown(key) then
            return true
        end
    end
    return false
end

local function measure_speed(dt)
    if not car or dt <= 0 then
        last_pos = nil
        speed = 0
        return
    end
    local position = car:GetPosition()
    if last_pos then
        speed = approach(speed, (position - last_pos):Length() / dt, 10, dt)
    end
    last_pos = position
end

--------------------------------------------------------------------------------
-- drawing
--------------------------------------------------------------------------------

-- a glass card low in the frame: the shop's name, what it offers and the key that opens it
local function draw_prompt(w, h, s, a)
    local title = "ZANTE AUTO"
    local sub   = "CAR WASH  \xC2\xB7  OIL CHANGE  \xC2\xB7  TYRES  \xC2\xB7  SERVICE"
    local bw    = math.max(ui.label_width(title, 18 * s), ui.label_width(sub, 11 * s)) + 230 * s
    local bh    = 76 * s
    local x     = w * 0.5 - bw * 0.5
    local y     = h * 0.78 + (1 - a) * 14 * s
    ui.panel(x, y, bw, bh, s, a, 0.86)
    Hud.Rect(x, y, 4 * s, bh, accent(a))
    ui.label(title, x + 30 * s, y + 18 * s, 18 * s, white(a))
    ui.label(sub, x + 30 * s, y + 46 * s, 11 * s, white(0.55 * a))
    local pw = Hud.TextWidth("ENTER", 12 * s, "plan_strong", 1 * s) + 16 * s
    local label_w = ui.label_width("SERVICE", 12 * s)
    local kx = x + bw - 30 * s - label_w - 10 * s - pw
    ui.key_pill("ENTER", kx, y + bh * 0.5 - 12 * s, s, a)
    ui.label("SERVICE", kx + pw + 10 * s, y + bh * 0.5 - 7 * s, 12 * s, accent(a))
end

local function draw_hint(w, h, s, a, text)
    local tw = ui.label_width(text, 12 * s)
    local x  = w * 0.5 - tw * 0.5 - 24 * s
    local y  = h * 0.82
    Hud.Rect(x, y, tw + 48 * s, 40 * s, ink(0.8 * a))
    Hud.Rect(x, y, 4 * s, 40 * s, accent(a))
    ui.label(text, w * 0.5 + 2 * s, y + 13 * s, 12 * s, white(0.9 * a), 0.5)
end

--------------------------------------------------------------------------------
-- the trip in and out
--------------------------------------------------------------------------------

local function enter_shop()
    CarTuning.PlaceAt(car, to_world(pit_spot), Quaternion.FromLookRotation(hub:GetForward(), Vector3(0, 1, 0)))
    CarTuning.SetHeld(car, true)
    shop = dofile(directory .. "plan_gt_auto.lua")
    shop.Open({ hub = hub })
end

local function leave_shop()
    if shop then
        shop.Close()
        shop = nil
    end
    if car then
        CarTuning.SetHeld(car, false)
    end
end

--------------------------------------------------------------------------------
-- lifecycle
--------------------------------------------------------------------------------

function entrance.Start()
    hub = World.GetEntityByName("gt_auto_hub")
    state, state_time, prompt, hint, speed, last_pos = "idle", 0, 0, 0, 0, nil
    if not ui.ready then
        ui.init()
        owns_ui = true
    end
end

function entrance.Stop()
    leave_shop()
    if owns_ui then
        ui.release()
        owns_ui = false
    end
    hub, car, state = nil, nil, "idle"
end

function entrance.IsBusy()
    return state ~= "idle"
end

function entrance.Tick()
    if not hub then
        return
    end
    local dt   = math.min(Timer.GetDeltaTimeRealSec(), 0.25)
    state_time = state_time + dt
    ui.load_fonts()
    local w, h = Hud.GetSize()
    if w <= 0 or h <= 0 then
        return
    end
    local s = clamp(math.min(h / 1080, w / 1920), 0.5, 2.0)

    if state == "entering" then
        Hud.Rect(0, 0, w, h, black(smooth(state_time / fade_in)))
        if state_time >= fade_in + 0.05 then
            ui.sound("transition", 0.8)
            enter_shop()
            state, state_time = "shop", 0
        end
        return
    elseif state == "shop" then
        local open = shop.Tick(dt)
        Hud.Rect(0, 0, w, h, black(1 - smooth(state_time / 0.5)))
        if not open then
            leave_shop()
            state, state_time = "leaving", 0
        end
        return
    elseif state == "leaving" then
        Hud.Rect(0, 0, w, h, black(1 - smooth(state_time / fade_out)))
        if state_time >= fade_out then
            state, state_time = "idle", 0
        end
        return
    end

    car = CarTuning.GetPlayerCar()
    measure_speed(dt)
    local driving    = car and CarTuning.IsOccupied(car)
    local at_service = driving and inside(forecourt, car:GetPosition()) and speed < max_speed
    prompt = approach(prompt, at_service and 1 or 0, 9, dt)

    -- on foot near the shop, point the player back to their car
    local camera  = World.GetCameraEntity()
    local on_foot = not driving and camera and inside(lot, camera:GetPosition())
    hint = approach(hint, on_foot and 1 or 0, 6, dt)

    if prompt > 0.01 then
        draw_prompt(w, h, s, prompt)
    elseif hint > 0.01 then
        draw_hint(w, h, s, hint, "DRIVE YOUR CAR ONTO THE FORECOURT FOR SERVICE")
    end

    if at_service and prompt > 0.6 and pressed(KeyCode.Enter) then
        ui.sound("confirm")
        state, state_time = "entering", 0
    end
end

return entrance
