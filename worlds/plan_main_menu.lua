--------------------------------------------------------------------------------
-- plan main menu
--
-- the title screen of the island. when play starts the view is taken over by a
-- cinematic camera that drifts through the player's garage and lounge while the
-- menu music loops, the player picks free roam and the camera hands back to the
-- player at their front door.
--
-- the look and the sounds come from plan_ui.lua, after gran turismo 4 and forza
-- motorsport 4: dark glass, thin tracked type, a white selection, glass chimes.
--
-- shots are authored in the garage's local space (player_garage_hub), so they
-- follow the building if it is ever moved or rotated.
--------------------------------------------------------------------------------

local menu = {}

local directory  = World.GetFilePath():match("^(.*[/\\])") or ""
local ui         = dofile(directory .. "plan_ui.lua")
local music_path = "project/plan_resources/main_menu/desert_house_take2.wav"

local clamp, lerp, smooth, ease_out, approach = ui.clamp, ui.lerp, ui.smooth, ui.ease_out, ui.approach
local white, ink, black, accent = ui.white, ui.ink, ui.black, ui.accent

-- garage local space: x across the workshop (lounge at +x), y up from the floor, z toward the road
local shots =
{
    { label = "THE GARAGE",        from = { 2.4, 0.50, 3.4 },  to = { 1.0, 0.42, 2.9 },  look_from = { -1.4, 0.55, -0.4 }, look_to = { -1.5, 0.55, -0.9 }, fov = 60, duration = 9.0 },
    { label = "THE LOUNGE",        from = { 6.6, 1.55, 4.8 },  to = { 8.6, 1.35, 4.9 },  look_from = { 12.0, 1.0, -6.0 },  look_to = { 13.0, 1.1, -6.5 },  fov = 64, duration = 10.0 },
    { label = "THE WORKSHOP",      from = { -9.2, 1.65, -4.4 }, to = { -5.8, 1.5, -5.2 }, look_from = { -6.8, 1.5, -10.0 }, look_to = { -4.2, 1.5, -10.0 }, fov = 66, duration = 9.0 },
    { label = "SLOW ROADS",        from = { 10.8, 1.0, 2.4 },  to = { 11.6, 1.2, 1.9 }, look_from = { 14.4, 1.0, -1.8 },  look_to = { 14.6, 1.1, -1.9 },  fov = 58, duration = 8.0 },
    { label = "MILES TO REMEMBER", from = { 1.6, 0.45, -4.4 }, to = { 1.6, 0.45, 1.6 },  look_from = { -1.4, 0.50, -2.8 }, look_to = { -1.4, 0.50, 0.4 },  fov = 56, duration = 10.0 },
    { label = "THE BAR",           from = { 9.6, 1.25, -3.6 }, to = { 11.2, 1.30, -4.2 }, look_from = { 12.6, 1.6, -9.8 }, look_to = { 13.8, 1.7, -9.8 },  fov = 58, duration = 9.0 },
    { label = "HOME",              from = { -3.0, 1.5, 21.0 }, to = { 3.5, 1.6, 20.6 },  look_from = { -1.6, 1.4, 4.0 },  look_to = { 2.8, 1.5, 2.5 },   fov = 48, duration = 10.0 },
}

local fade_time = 0.8

-- state
local state         = "off"
local state_time    = 0.0
local steady        = 0.0
local time          = 0.0
local selected      = 1
local select_anim   = {}
local shot_index    = 1
local shot_time     = 0.0
local music_volume  = 0.8
local music_gain    = 0.0
local camera_entity = nil
local music_entity  = nil
local music_source  = nil
local hub           = nil
local jukebox_audio = nil
local mouse_last_x  = -1
local mouse_last_y  = -1

local function items()
    local list =
    {
        { group = "main", id = "play",  label = "FREE ROAM",  hint = "Take the island, starting from your front door" },
        { group = "main", id = "music", label = "SOUNDTRACK", hint = "Desert House, take two" },
    }
    if not Engine.IsEditorVisible() then
        table.insert(list, { group = "main", id = "quit", label = "QUIT", hint = "Park it for today" })
    end
    return list
end

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local function to_world(p)
    if not hub then
        return Vector3(p[1], p[2], p[3])
    end
    local origin = hub:GetPosition()
    return origin + hub:GetRight() * p[1] + hub:GetUp() * p[2] + hub:GetForward() * p[3]
end

local function lerp_point(a, b, t)
    return { lerp(a[1], b[1], t), lerp(a[2], b[2], t), lerp(a[3], b[3], t) }
end

local function format_clock()
    local hours = World.GetTimeOfDay() * 24.0
    local h = math.floor(hours) % 24
    local m = math.floor((hours - math.floor(hours)) * 60)
    return string.format("%02d:%02d", h, m)
end

local function credits()
    return _G.plan_garage and _G.plan_garage.credits or 0
end

--------------------------------------------------------------------------------
-- camera
--------------------------------------------------------------------------------

local function create_camera()
    camera_entity = World.CreateEntity()
    camera_entity:SetName("main_menu_camera")
    camera_entity:SetTransient(true)
    local camera = camera_entity:AddComponent(ComponentType.Camera)
    if camera then
        camera:SetFlag(CameraFlags.CanBeControlled, false)
    end
    World.SetActiveCamera(camera_entity)
end

local function release_camera()
    if World.GetActiveCameraOverride() == camera_entity then
        World.SetActiveCamera(nil)
    end
    if camera_entity then
        World.RemoveEntity(camera_entity)
        camera_entity = nil
    end
end

local function tick_camera(dt)
    if not camera_entity then
        return
    end

    shot_time = shot_time + dt
    local shot = shots[shot_index]
    if shot_time >= shot.duration then
        shot_time  = shot_time - shot.duration
        shot_index = shot_index % #shots + 1
        shot       = shots[shot_index]
    end

    -- mostly linear drift with soft ends, plus a breath of handheld sway so the frame never feels locked off
    local t      = shot_time / shot.duration
    local e      = lerp(t, smooth(t), 0.5)
    local sway   = { math.sin(time * 0.71) * 0.018, math.sin(time * 0.53 + 1.3) * 0.012, math.sin(time * 0.37 + 2.1) * 0.015 }
    local eye    = lerp_point(shot.from, shot.to, e)
    local target = lerp_point(shot.look_from, shot.look_to, e)
    eye          = { eye[1] + sway[1], eye[2] + sway[2], eye[3] + sway[3] }

    local eye_world    = to_world(eye)
    local target_world = to_world(target)
    camera_entity:SetPosition(eye_world)
    camera_entity:SetRotation(Quaternion.FromLookRotation(target_world - eye_world, Vector3(0, 1, 0)))

    local camera = camera_entity:GetComponent(ComponentType.Camera)
    if camera and shot.fov then
        camera:SetFovHorizontalDeg(shot.fov + math.sin(t * math.pi) * 1.5)
    end
end

-- dip to black at every cut, so the cut itself is never seen
local function cut_fade()
    local shot = shots[shot_index]
    local into = 1 - smooth(shot_time / fade_time)
    local out  = smooth((shot_time - (shot.duration - fade_time)) / fade_time)
    return math.max(into, out)
end

--------------------------------------------------------------------------------
-- music
--------------------------------------------------------------------------------

local function start_music()
    music_entity = World.CreateEntity()
    music_entity:SetName("main_menu_music")
    music_entity:SetTransient(true)
    music_source = music_entity:AddComponent(ComponentType.AudioSource)
    if music_source then
        music_source:SetPlayOnStart(false)
        music_source:SetIs3d(false)
        music_source:SetLoop(true)
        music_source:SetAudioClip(music_path)
        music_source:SetVolume(0.0)
        music_source:PlayClip()
    end
    music_gain = 0.0
end

local function stop_music()
    if music_source then
        music_source:StopClip()
    end
    if music_entity then
        World.RemoveEntity(music_entity)
    end
    music_entity = nil
    music_source = nil
end

local function tick_music(dt, target)
    music_gain = approach(music_gain, target, 1.6, dt)
    if music_source then
        music_source:SetVolume(clamp(music_gain * music_volume, 0, 1))
    end
end

--------------------------------------------------------------------------------
-- drawing
--------------------------------------------------------------------------------

local function draw_backdrop(w, h, s, alpha)
    -- only the left third is shaded for the list, the reel stays open and bright on the right
    Hud.RectGradient(0, 0, w * 0.58, h, ink(0.72 * alpha), ink(0.0), ink(0.0), ink(0.72 * alpha))
    Hud.RectGradient(0, 0, w * 0.34, h, ink(0.35 * alpha), ink(0.0), ink(0.0), ink(0.35 * alpha))
    Hud.RectGradient(0, h * 0.7, w, h * 0.3, ink(0.0), ink(0.0), ink(0.45 * alpha), ink(0.45 * alpha))
end

-- the title draws in on a blue rule that sweeps across the screen, its letters closing up as it settles
local function draw_title(w, s, x, y, reveal)
    local sweep = ease_out((reveal - 0.1) / 0.9)
    local hold  = 1 - smooth((reveal - 1.2) / 0.8)
    if sweep > 0 and hold > 0 then
        Hud.Rect(0, y + 132 * s, w * sweep, 1 * s, accent(0.9 * hold))
        Hud.Rect(w * sweep - 120 * s, y + 131 * s, 120 * s, 3 * s, accent(0.6 * hold))
    end

    local a1 = ease_out((reveal - 0.3) / 0.8)
    local a2 = ease_out((reveal - 0.45) / 1.4)
    local a3 = ease_out((reveal - 0.9) / 0.8)
    ui.label("KARABELAS MOTORWORKS", x + 4 * s, y, 13 * s, accent(a1))
    ui.label("EST. 2002", x + 4 * s + ui.label_width("KARABELAS MOTORWORKS", 13 * s) + 22 * s, y, 13 * s, white(0.45 * a1))
    Hud.Text("ZAKYNTHOS", x - 4 * s, y + 18 * s, 96 * s, white(a2), "plan_thin", 0, lerp(44, 18, a2) * s)

    ui.hairline(x, y + 132 * s, 520 * s * a3, s, a3)
    Hud.Rect(x, y + 131 * s, 56 * s * a3, 3 * s, accent(a3))
    ui.label("OPEN ROADS", x, y + 152 * s, 12 * s, white(0.7 * a3))
    local tx = x + ui.label_width("OPEN ROADS", 12 * s) + 26 * s
    ui.label("OPEN WATER", tx, y + 152 * s, 12 * s, white(0.7 * a3))
    tx = tx + ui.label_width("OPEN WATER", 12 * s) + 26 * s
    ui.label("YOUR ISLAND", tx, y + 152 * s, 12 * s, white(0.7 * a3))
end

local function draw_volume(x, y, s, k, a)
    local segments = 10
    local filled   = math.floor(music_volume * segments + 0.5)
    for i = 1, segments do
        local on  = i <= filled
        local col = on and ui.on_bar(k, a, 0.8) or ui.on_bar(k, 0.18 * a, 0.9)
        if on and k > 0.5 then
            col = accent(a)
        end
        Hud.Rect(x + (i - 1) * 9 * s, y, 5 * s, 16 * s, col)
    end
    local text = string.format("%d", math.floor(music_volume * 100 + 0.5))
    Hud.Text(text, x + segments * 9 * s + 10 * s, y - 2 * s, 16 * s, ui.on_bar(k, a, 0.6), "plan_regular")
end

local function draw_menu(s, x, y, reveal, list, dt)
    local bar_w, bar_h, gap = 500 * s, 62 * s, 6 * s
    local cy = y
    for i, item in ipairs(list) do
        local a = ease_out((reveal - 1.0 - i * 0.07) / 0.6)
        select_anim[i] = approach(select_anim[i] or 0, i == selected and 1 or 0, 16, dt)
        local k  = select_anim[i]
        local rx = x + (1 - a) * -24 * s
        ui.list_bar(rx, cy, bar_w, bar_h, s, k, a)

        Hud.Text(string.format("%02d", i), rx + 22 * s, cy + bar_h * 0.5 - 8 * s, 13 * s, k > 0.5 and accent(a) or white(0.35 * a), "plan_regular", 0, 1 * s)
        local label_y = lerp(cy + bar_h * 0.5 - 11 * s, cy + 10 * s, k)
        ui.label(item.label, rx + 62 * s, label_y, 18 * s, ui.on_bar(k, a))
        if k > 0.05 then
            Hud.Text(item.hint, rx + 62 * s, cy + 36 * s, 13 * s, ink(0.6 * k * a), "plan_regular")
        end
        if item.id == "music" then
            draw_volume(rx + bar_w - 150 * s, cy + bar_h * 0.5 - 8 * s, s, k, a)
        end

        item.bounds = { rx, cy, bar_w, bar_h }
        cy = cy + bar_h + gap
    end
end

local function draw_now_playing(x, y, fh, alpha)
    local s = fh / ui.footer_height
    for i = 1, 4 do
        local v  = 0.3 + 0.7 * math.abs(math.sin(time * (2.1 + i * 0.73) + i * 1.7) * math.sin(time * (0.9 + i * 0.31)))
        local bh = 14 * s * v * music_gain
        Hud.Rect(x + (i - 1) * 5 * s, y + fh * 0.5 + 7 * s - bh, 3 * s, math.max(bh, 1 * s), accent(alpha))
    end
    local tx = x + 34 * s
    tx = tx + ui.label("NOW PLAYING", tx, y + fh * 0.5 - 6 * s, 11 * s, white(0.45 * alpha)) + 16 * s
    Hud.Text("Desert House", tx, y + fh * 0.5 - 10 * s, 17 * s, white(0.95 * alpha), "plan_regular")
    tx = tx + Hud.TextWidth("Desert House", 17 * s, "plan_regular") + 12 * s
    Hud.Text("Take 2", tx, y + fh * 0.5 - 10 * s, 17 * s, white(0.5 * alpha), "plan_regular")
end

-- the current shot's name over a segmented reel, bottom right above the prompts
local function draw_reel(w, h, s, alpha)
    local x = w - ui.margin * s
    local y = h - ui.footer_height * s - 60 * s
    local shot = shots[shot_index]
    local a    = (1 - cut_fade()) * alpha
    ui.label(shot.label, x, y, 12 * s, white(0.85 * a), 1)
    ui.label(string.format("%02d / %02d", shot_index, #shots), x - ui.label_width(shot.label, 12 * s) - 18 * s, y, 12 * s, white(0.4 * alpha), 1)

    local tick_w = 26 * s
    local gap    = 4 * s
    local left   = x - #shots * (tick_w + gap) + gap
    for i = 1, #shots do
        local dx = left + (i - 1) * (tick_w + gap)
        Hud.Rect(dx, y + 26 * s, tick_w, 2 * s, white(0.2 * alpha))
        if i < shot_index then
            Hud.Rect(dx, y + 26 * s, tick_w, 2 * s, white(0.6 * alpha))
        elseif i == shot_index then
            Hud.Rect(dx, y + 26 * s, tick_w * clamp(shot_time / shot.duration, 0, 1), 2 * s, accent(alpha))
        end
    end
end

local function draw_chrome(w, h, s, alpha, list)
    local slide = 1 - alpha
    ui.header(w, s, alpha, { "ZAKYNTHOS", "HOME" }, { { "CREDITS", ui.credits(credits()) }, { "LOCAL TIME", format_clock() } }, slide)
    local prompts = { { "\xE2\x86\x91 \xE2\x86\x93", "SELECT" } }
    if list[selected] and list[selected].id == "music" then
        table.insert(prompts, { "\xE2\x86\x90 \xE2\x86\x92", "VOLUME" })
    end
    table.insert(prompts, { "ENTER", "CONFIRM" })
    ui.footer(w, h, s, alpha, prompts, function(x, y, fh) draw_now_playing(x, y, fh, alpha) end, slide)
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

local function select_item(index, count)
    index = (index - 1) % count + 1
    if index ~= selected then
        selected = index
        ui.sound("move")
    end
end

local function adjust_volume(direction)
    local before = music_volume
    music_volume = clamp(music_volume + direction * 0.1, 0, 1)
    if music_volume ~= before then
        ui.sound("tick", 0.9, 0.8 + music_volume * 0.5)
    else
        ui.sound("error", 0.5)
    end
end

local function activate(item)
    if item.id == "play" then
        ui.sound("confirm")
        ui.sound("transition", 0.8)
        state, state_time = "leaving", 0
    elseif item.id == "music" then
        adjust_volume(music_volume >= 0.999 and -10 or 1)
    elseif item.id == "quit" then
        ui.sound("back")
        state, state_time = "quitting", 0
    end
end

local function handle_input(list)
    local count = #list
    local mx, my = Hud.GetMousePosition()

    if pressed(KeyCode.Arrow_Down, KeyCode.S, KeyCode.DPad_Down) then
        select_item(selected + 1, count)
    end
    if pressed(KeyCode.Arrow_Up, KeyCode.W, KeyCode.DPad_Up) then
        select_item(selected - 1, count)
    end

    local item = list[selected]
    if item.id == "music" then
        if pressed(KeyCode.Arrow_Right, KeyCode.D, KeyCode.DPad_Right) then adjust_volume(1) end
        if pressed(KeyCode.Arrow_Left, KeyCode.A, KeyCode.DPad_Left) then adjust_volume(-1) end
    end

    -- the mouse only takes over the selection when it actually moves, so it never fights the keyboard
    local hovered = nil
    for i, entry in ipairs(list) do
        local b = entry.bounds
        if b and mx >= b[1] and mx <= b[1] + b[3] and my >= b[2] and my <= b[2] + b[4] then
            hovered = i
        end
    end
    local moved = math.abs(mx - mouse_last_x) + math.abs(my - mouse_last_y) > 0.5
    mouse_last_x, mouse_last_y = mx, my
    if hovered and moved then
        select_item(hovered, count)
    end

    if pressed(KeyCode.Enter, KeyCode.Space, KeyCode.Button_South) then
        activate(list[selected])
    elseif hovered and pressed(KeyCode.Click_Left) then
        selected = hovered
        activate(list[selected])
    end
end

--------------------------------------------------------------------------------
-- lifecycle
--------------------------------------------------------------------------------

function menu.Start()
    hub = World.GetEntityByName("player_garage_hub")
    local jukebox = World.GetEntityByName("Jukebox - Slow Roads")
    jukebox_audio = jukebox and jukebox:GetComponent(ComponentType.AudioSource) or nil
    if jukebox_audio then
        jukebox_audio:SetMute(true)
    end

    _G.plan_ui = ui
    ui.init()
    state, state_time, steady = "warmup", 0, 0
    time, shot_index, shot_time = 0, 1, 0
    selected, select_anim = 1, {}
    create_camera()
    start_music()
    tick_camera(0)
end

function menu.Stop()
    release_camera()
    stop_music()
    ui.release()
    _G.plan_ui = nil
    if jukebox_audio then
        jukebox_audio:SetMute(false)
        jukebox_audio = nil
    end
    hub   = nil
    state = "off"
end

-- ends the menu at once as if free roam was picked, the ui stays loaded for the rest of play
function menu.Dismiss()
    if state == "off" or state == "done" then
        return
    end
    release_camera()
    stop_music()
    if jukebox_audio then
        jukebox_audio:SetMute(false)
    end
    state = "done"
end

-- true until the player has been handed back their view
function menu.IsActive()
    return state ~= "off" and state ~= "done"
end

function menu.Tick()
    if state == "off" or state == "done" then
        return
    end

    -- a sequence played from the editor owns the screen, the menu would draw over it
    if Sequencer and Sequencer.IsAvailable() and Sequencer.IsPlaying() then
        menu.Dismiss()
        return
    end

    -- real time, so the reel and fades keep their pace whatever the world's time scale or frame rate
    local dt_real = Timer.GetDeltaTimeRealSec()
    local dt      = math.min(dt_real, 0.25)

    ui.load_fonts()

    -- the first frames of play stream the island and hitch, hold on black (music already rising, exposure settling
    -- on the first shot) until frames run steady, so the intro is never seen stuttering
    if state == "warmup" then
        state_time = state_time + dt_real
        steady     = dt_real < 0.1 and steady + dt_real or 0
        tick_camera(0)
        tick_music(dt, 0.6)
        local w, h = Hud.GetSize()
        Hud.Rect(0, 0, w, h, black(1))
        if steady > 0.6 or state_time > 8.0 then
            state, state_time, time = "intro", 0, 0
            ui.sound("intro", 0.8)
        end
        return
    end

    time       = time + dt
    state_time = state_time + dt
    local w, h = Hud.GetSize()
    if w <= 0 or h <= 0 then
        return
    end
    local s    = clamp(math.min(h / 1080, w / 1920), 0.5, 2.0)
    local list = items()
    selected   = clamp(selected, 1, #list)
    local left = 120 * s
    local top  = h * 0.24

    if state == "intro" or state == "menu" then
        if state == "intro" and state_time > 3.0 then
            state = "menu"
        end
        tick_camera(dt)
        tick_music(dt, 1.0)

        local reveal = state == "intro" and time or 10
        local chrome = smooth((reveal - 0.2) / 0.9)
        draw_backdrop(w, h, s, smooth(reveal / 1.2))
        Hud.Rect(0, 0, w, h, black(cut_fade() * 0.85))
        Hud.Rect(0, 0, w, h, black(1 - smooth(reveal / 1.6)))

        draw_title(w, s, left, top, reveal)
        draw_menu(s, left, top + 230 * s, reveal, list, dt)
        draw_reel(w, h, s, chrome)
        draw_chrome(w, h, s, chrome, list)

        if state == "menu" or time > 1.5 then
            handle_input(list)
        end
    elseif state == "leaving" or state == "quitting" then
        -- the menu falls away, the screen goes black and the island takes over from the player's eyes
        tick_camera(dt)
        tick_music(dt, 0.0)
        local k = smooth(state_time / 1.0)
        draw_backdrop(w, h, s, 1 - k)
        draw_chrome(w, h, s, 1 - k, list)
        Hud.Rect(0, 0, w, h, black(math.max(k, cut_fade() * 0.85)))
        if state_time >= 1.2 then
            if state == "quitting" then
                Engine.Quit()
                state = "done"
                return
            end
            release_camera()
            state, state_time = "arriving", 0
        end
    elseif state == "arriving" then
        tick_music(dt, 0.0)
        Hud.Rect(0, 0, w, h, black(1 - smooth(state_time / 1.4)))
        if state_time >= 1.4 then
            stop_music()
            if jukebox_audio then
                jukebox_audio:SetMute(false)
            end
            state = "done"
        end
    end
end

return menu
