--------------------------------------------------------------------------------
-- plan main menu
--
-- the title screen of the island. when play starts the view is taken over by a
-- cinematic camera that drifts through the player's garage and lounge while the
-- menu music loops, the player picks free roam and the camera hands back to the
-- player at their front door.
--
-- shots are authored in the garage's local space (player_garage_hub), so they
-- follow the building if it is ever moved or rotated.
--------------------------------------------------------------------------------

local menu = {}

local music_path   = "project/plan_resources/main_menu/desert_house_take2.wav"
local font_display = "data/fonts/OpenSans/OpenSans-ExtraBoldItalic.ttf"
local font_light   = "data/fonts/OpenSans/OpenSans-Light.ttf"
local font_medium  = "data/fonts/OpenSans/OpenSans-SemiBold.ttf"

local accent = { 1.0, 0.58, 0.20 }

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
local fonts_ready   = false
local mouse_last_x  = -1
local mouse_last_y  = -1

local function items()
    local list =
    {
        { id = "play",  label = "FREE ROAM",  hint = "Take the island, starting from your front door" },
        { id = "music", label = "SOUNDTRACK", hint = "Desert House, take two" },
    }
    if not Engine.IsEditorVisible() then
        table.insert(list, { id = "quit", label = "QUIT", hint = "Park it for today" })
    end
    return list
end

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

local function clamp(x, a, b) return math.max(a, math.min(b, x)) end
local function lerp(a, b, t) return a + (b - a) * t end
local function smooth(t) t = clamp(t, 0, 1) return t * t * (3 - 2 * t) end
local function ease_out(t) t = clamp(t, 0, 1) return 1 - (1 - t) ^ 3 end
local function approach(current, target, rate, dt) return current + (target - current) * (1 - math.exp(-rate * dt)) end

local function rgba(r, g, b, a) return Vector4(r, g, b, a) end
local function white(a) return Vector4(1, 1, 1, a) end
local function black(a) return Vector4(0, 0, 0, a) end
local function amber(a) return Vector4(accent[1], accent[2], accent[3], a) end

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

local function load_fonts()
    if fonts_ready then
        return
    end
    Hud.LoadFont("display", font_display)
    Hud.LoadFont("light", font_light)
    Hud.LoadFont("medium", font_medium)
    fonts_ready = true
end

local function draw_backdrop(w, h, s, alpha)
    -- legibility: a deep wash from the left edge, a floor under the footer and a soft cinematic top
    Hud.RectGradient(0, 0, w * 0.62, h, black(0.82 * alpha), black(0.0), black(0.0), black(0.82 * alpha))
    Hud.RectGradient(0, 0, w * 0.42, h, black(0.45 * alpha), black(0.0), black(0.0), black(0.45 * alpha))
    Hud.RectGradient(w * 0.55, 0, w * 0.45, h * 0.32, black(0.0), black(0.7 * alpha), black(0.0), black(0.0))
    Hud.RectGradient(0, h * 0.62, w, h * 0.38, black(0.0), black(0.0), black(0.75 * alpha), black(0.75 * alpha))
    Hud.RectGradient(0, 0, w, h * 0.18, black(0.55 * alpha), black(0.55 * alpha), black(0.0), black(0.0))

    -- letterbox slides in with the intro
    local bar = h * 0.055 * alpha
    Hud.Rect(0, 0, w, bar, black(1.0))
    Hud.Rect(0, h - bar, w, bar, black(1.0))
end

local function draw_title(s, x, y, reveal)
    local a1 = ease_out((reveal - 0.0) / 0.8)
    local a2 = ease_out((reveal - 0.25) / 0.9)
    local a3 = ease_out((reveal - 0.55) / 0.9)

    Hud.Text("KARABELAS MOTORWORKS   \xC2\xB7   EST. 2002", x + 6 * s + (1 - a1) * -30 * s, y, 19 * s, amber(a1), "mono", 0, 6 * s)
    Hud.Text("ZAKYNTHOS", x + (1 - a2) * -60 * s, y + 20 * s, 132 * s, white(a2), "display", 0, 1 * s)
    local line_w = 110 * s * a3
    Hud.Rect(x + 8 * s, y + 194 * s, line_w, 4 * s, amber(a3))
    Hud.Text("OPEN ROADS   \xC2\xB7   OPEN WATER   \xC2\xB7   YOUR ISLAND", x + 8 * s, y + 214 * s, 22 * s, white(0.8 * a3), "light", 0, 5 * s)
end

local function draw_volume(x, y, s, alpha, active)
    local segments = 10
    local filled   = math.floor(music_volume * segments + 0.5)
    for i = 1, segments do
        local on = i <= filled
        local a  = on and (active and 1.0 or 0.6) or 0.18
        local col = on and (active and amber(a * alpha) or white(a * alpha)) or white(a * alpha)
        Hud.Rect(x + (i - 1) * 12 * s, y, 8 * s, 20 * s, col, 1 * s)
    end
    Hud.Text(string.format("%d%%", math.floor(music_volume * 100 + 0.5)), x + segments * 12 * s + 12 * s, y - 1 * s, 20 * s, white((active and 0.95 or 0.5) * alpha), "mono")
end

local function draw_menu(s, x, y, reveal, list, dt)
    local row_h = 84 * s
    local mx, my = Hud.GetMousePosition()
    for i, item in ipairs(list) do
        select_anim[i] = approach(select_anim[i] or 0, i == selected and 1 or 0, 14, dt)
        local k    = select_anim[i]
        local a    = ease_out((reveal - 0.9 - i * 0.12) / 0.7)
        local ry   = y + (i - 1) * row_h
        local rx   = x + (1 - a) * -40 * s

        -- selection: a soft amber wash, a bright bar and the label stepping forward
        if k > 0.01 then
            Hud.RectGradient(rx - 24 * s, ry - 8 * s, 560 * s * k, row_h - 6 * s, amber(0.20 * k * a), amber(0.0), amber(0.0), amber(0.20 * k * a))
            Hud.Rect(rx - 24 * s, ry - 8 * s, 4 * s, (row_h - 6 * s) * k, amber(a))
        end

        local size  = lerp(32, 40, k) * s
        local label = Hud.Text(item.label, rx + 16 * s * k, ry, size, white(lerp(0.55, 1.0, k) * a), "medium", 0, lerp(4, 6, k) * s)
        Hud.Text(string.format("%02d", i), rx - 62 * s, ry + 13 * s, 17 * s, (i == selected) and amber(a) or white(0.35 * a), "mono", 0, 2 * s)

        if item.id == "music" then
            draw_volume(rx + 16 * s * k + label + 34 * s, ry + 16 * s, s, a, i == selected)
        end
        if k > 0.05 then
            Hud.Text(item.hint, rx + 16 * s * k, ry + 50 * s, 19 * s, white(0.7 * k * a), "light", 0, 0.5 * s)
        end

        item.bounds = { rx - 24 * s, ry - 8 * s, 620 * s, row_h - 6 * s }
    end
    return mx, my
end

local function draw_now_playing(w, h, s, alpha)
    local x = 120 * s
    local y = h - h * 0.055 - 92 * s

    -- equalizer, a few springy bars that never repeat in an obvious way
    for i = 1, 5 do
        local v = 0.35 + 0.65 * math.abs(math.sin(time * (2.1 + i * 0.73) + i * 1.7) * math.sin(time * (0.9 + i * 0.31)))
        local bh = 26 * s * v * music_gain
        Hud.Rect(x + (i - 1) * 7 * s, y + 30 * s - bh, 4 * s, math.max(bh, 2 * s), amber(alpha))
    end

    Hud.Text("NOW PLAYING", x + 52 * s, y - 6 * s, 15 * s, amber(alpha), "mono", 0, 5 * s)
    Hud.Text("Desert House", x + 52 * s, y + 12 * s, 27 * s, white(0.95 * alpha), "medium", 0, 0.5 * s)
    Hud.Text("TAKE 2   \xC2\xB7   ON REPEAT", x + 54 * s, y + 48 * s, 14 * s, white(0.55 * alpha), "mono", 0, 3 * s)
end

local function draw_keycap(text, x, y, s, alpha)
    local tw = Hud.TextWidth(text, 16 * s, "mono")
    local cw = math.max(tw + 18 * s, 34 * s)
    Hud.Rect(x, y, cw, 32 * s, white(0.08 * alpha), 5 * s)
    Hud.RectOutline(x, y, cw, 32 * s, white(0.55 * alpha), 1.3 * s, 5 * s)
    Hud.Text(text, x + cw * 0.5, y + 6 * s, 16 * s, white(0.9 * alpha), "mono", 0.5)
    return cw
end

local function draw_hints(w, h, s, alpha)
    local y = h - h * 0.055 - 62 * s
    local x = w - 120 * s
    local entries = { { "ENTER", "CONFIRM" }, { "\xE2\x86\x90 \xE2\x86\x92", "ADJUST" }, { "\xE2\x86\x91 \xE2\x86\x93", "SELECT" } }
    for _, entry in ipairs(entries) do
        local lw = Hud.TextWidth(entry[2], 16 * s, "mono", 3 * s)
        x = x - lw
        Hud.Text(entry[2], x, y + 6 * s, 16 * s, white(0.7 * alpha), "mono", 0, 3 * s)
        local kw = Hud.TextWidth(entry[1], 16 * s, "mono")
        x = x - math.max(kw + 18 * s, 34 * s) - 12 * s
        draw_keycap(entry[1], x, y, s, alpha)
        x = x - 40 * s
    end
end

local function draw_location(w, h, s, alpha)
    local x = w - 120 * s
    local y = h * 0.055 + 46 * s
    Hud.Text(format_clock(), x, y, 52 * s, white(0.95 * alpha), "light", 1, 2 * s)
    Hud.Text("PLAYER HOME   \xC2\xB7   ZAKYNTHOS", x, y + 70 * s, 16 * s, amber(alpha), "mono", 1, 4 * s)

    -- the current shot's name, it fades with each cut
    local shot = shots[shot_index]
    local a    = (1 - cut_fade()) * alpha
    Hud.Text(shot.label, x, y + 96 * s, 19 * s, white(0.75 * a), "light", 1, 6 * s)

    -- progress through the reel, the current shot's tick fills as it plays
    local tick_w = 22 * s
    local gap    = 6 * s
    local left   = x - #shots * (tick_w + gap) + gap
    for i = 1, #shots do
        local dx = left + (i - 1) * (tick_w + gap)
        Hud.Rect(dx, y + 132 * s, tick_w, 3 * s, white(0.25 * alpha))
        if i < shot_index then
            Hud.Rect(dx, y + 132 * s, tick_w, 3 * s, white(0.6 * alpha))
        elseif i == shot_index then
            Hud.Rect(dx, y + 132 * s, tick_w * clamp(shot_time / shot.duration, 0, 1), 3 * s, amber(alpha))
        end
    end
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

local function adjust_volume(direction)
    music_volume = clamp(music_volume + direction * 0.1, 0, 1)
end

local function activate(item)
    if item.id == "play" then
        state, state_time = "leaving", 0
    elseif item.id == "music" then
        music_volume = music_volume >= 0.999 and 0 or clamp(music_volume + 0.1, 0, 1)
    elseif item.id == "quit" then
        state, state_time = "quitting", 0
    end
end

local function handle_input(list, mx, my)
    local count = #list
    if pressed(KeyCode.Arrow_Down, KeyCode.S, KeyCode.DPad_Down) then
        selected = selected % count + 1
    end
    if pressed(KeyCode.Arrow_Up, KeyCode.W, KeyCode.DPad_Up) then
        selected = (selected - 2) % count + 1
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
        selected = hovered
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
    if jukebox_audio then
        jukebox_audio:SetMute(false)
        jukebox_audio = nil
    end
    hub   = nil
    state = "off"
end

function menu.Tick()
    if state == "off" or state == "done" then
        return
    end

    -- real time, so the reel and fades keep their pace whatever the world's time scale or frame rate
    local dt_real = Timer.GetDeltaTimeRealSec()
    local dt      = math.min(dt_real, 0.25)

    load_fonts()

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

    if state == "intro" or state == "menu" then
        if state == "intro" and state_time > 2.0 then
            state = "menu"
        end
        tick_camera(dt)
        tick_music(dt, 1.0)

        local reveal = state_time + (state == "menu" and 10 or 0)
        local ui     = smooth((time - 0.6) / 1.4)
        draw_backdrop(w, h, s, ui)
        Hud.Rect(0, 0, w, h, black(cut_fade() * 0.9))
        Hud.Rect(0, 0, w, h, black(1 - smooth(time / 2.2)))

        local left = 140 * s
        draw_title(s, left, h * 0.25, time - 0.8)
        local mx, my = draw_menu(s, left, h * 0.25 + 300 * s, time - 0.8, list, dt)
        draw_now_playing(w, h, s, ui)
        draw_hints(w, h, s, ui)
        draw_location(w, h, s, ui)

        if state == "menu" or time > 1.5 then
            handle_input(list, mx, my)
        end
    elseif state == "leaving" or state == "quitting" then
        -- the menu falls away, the screen goes black and the island takes over from the player's eyes
        tick_camera(dt)
        tick_music(dt, 0.0)
        local k = smooth(state_time / 1.0)
        draw_backdrop(w, h, s, 1 - k)
        Hud.Rect(0, 0, w, h, black(math.max(k, cut_fade() * 0.9)))
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
