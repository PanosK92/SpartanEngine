--------------------------------------------------------------------------------
-- plan ui
--
-- the island's menu language, shared by the main menu and the tune shop. it is
-- clinical on purpose, after the menus of gran turismo 4 and forza motorsport 4:
-- cool dark glass, thin tracked type, one blue accent, hairline rules, and a
-- selection that flips to solid white with dark text. the sounds are a matching
-- set of glass pips, chimes and soft air, synthesized offline by
-- project/plan_resources/main_menu/sources/generate_ui_sounds.py.
--------------------------------------------------------------------------------

local ui = {}

local sound_dir = "project/plan_resources/main_menu/ui/"
local sounds    = { "move", "tick", "tab", "confirm", "back", "transition", "purchase", "fitting", "error", "intro" }
local voices_per_sound = 3

local palette =
{
    ink    = { 0.030, 0.038, 0.052 },
    paper  = { 0.945, 0.955, 0.970 },
    accent = { 0.28, 0.62, 1.00 },
    good   = { 0.40, 0.86, 0.62 },
    bad    = { 1.00, 0.42, 0.42 },
}
ui.palette = palette

ui.header_height = 64
ui.footer_height = 52
ui.margin        = 64
ui.volume        = 0.9

local voices      = {}
local voice_next  = {}
local fonts_ready = false

--------------------------------------------------------------------------------
-- math and colour
--------------------------------------------------------------------------------

function ui.clamp(x, a, b) return math.max(a, math.min(b, x)) end
function ui.lerp(a, b, t) return a + (b - a) * t end
function ui.smooth(t) t = ui.clamp(t, 0, 1) return t * t * (3 - 2 * t) end
function ui.ease_out(t) t = ui.clamp(t, 0, 1) return 1 - (1 - t) ^ 3 end
function ui.approach(current, target, rate, dt) return current + (target - current) * (1 - math.exp(-rate * dt)) end

function ui.color(c, a) return Vector4(c[1], c[2], c[3], a) end
function ui.white(a) return Vector4(palette.paper[1], palette.paper[2], palette.paper[3], a) end
function ui.ink(a) return Vector4(palette.ink[1], palette.ink[2], palette.ink[3], a) end
function ui.black(a) return Vector4(0, 0, 0, a) end
function ui.accent(a) return Vector4(palette.accent[1], palette.accent[2], palette.accent[3], a) end
function ui.good(a) return ui.color(palette.good, a) end
function ui.bad(a) return ui.color(palette.bad, a) end

-- text that sits on a list bar, white on glass that turns to ink as the bar turns white
function ui.on_bar(k, a, dim)
    local light = dim or 0.62
    local r = ui.lerp(palette.paper[1], palette.ink[1], k)
    local g = ui.lerp(palette.paper[2], palette.ink[2], k)
    local b = ui.lerp(palette.paper[3], palette.ink[3], k)
    return Vector4(r, g, b, ui.lerp(light, 1.0, k) * a)
end

function ui.credits(value)
    local text = tostring(math.floor(value))
    while true do
        local replaced, count = text:gsub("^(%d+)(%d%d%d)", "%1,%2")
        text = replaced
        if count == 0 then
            break
        end
    end
    return "Cr. " .. text
end

--------------------------------------------------------------------------------
-- fonts
--------------------------------------------------------------------------------

function ui.load_fonts()
    if fonts_ready then
        return
    end
    Hud.LoadFont("plan_thin", "data/fonts/OpenSans/OpenSans-Light.ttf")
    Hud.LoadFont("plan_regular", "data/fonts/Inter/Inter-Regular.ttf")
    Hud.LoadFont("plan_strong", "data/fonts/Inter/Inter-SemiBold.ttf")
    fonts_ready = true
end

-- the small tracked capitals that name every section
function ui.label(text, x, y, size, color, align)
    return Hud.Text(text, x, y, size, color, "plan_strong", align or 0, size * 0.24)
end

function ui.label_width(text, size)
    return Hud.TextWidth(text, size, "plan_strong", size * 0.24)
end

--------------------------------------------------------------------------------
-- sound
--------------------------------------------------------------------------------

function ui.init()
    if ui.ready then
        return
    end
    for _, name in ipairs(sounds) do
        voices[name], voice_next[name] = {}, 1
        for i = 1, voices_per_sound do
            local entity = World.CreateEntity()
            entity:SetName("plan_ui_" .. name .. "_" .. i)
            entity:SetTransient(true)
            local source = entity:AddComponent(ComponentType.AudioSource)
            if source then
                source:SetPlayOnStart(false)
                source:SetIs3d(false)
                source:SetLoop(false)
                source:SetAudioClip(sound_dir .. name .. ".wav")
                table.insert(voices[name], { entity = entity, source = source })
            end
        end
    end
    ui.ready = true
end

function ui.release()
    for _, list in pairs(voices) do
        for _, voice in ipairs(list) do
            voice.source:StopClip()
            World.RemoveEntity(voice.entity)
        end
    end
    voices, voice_next = {}, {}
    ui.ready = false
end

-- voices rotate, so a quick run of cursor steps never cuts its own tail off
function ui.sound(name, volume, pitch)
    local list = voices[name]
    if not list or #list == 0 then
        return
    end
    local index = voice_next[name]
    voice_next[name] = index % #list + 1
    local source = list[index].source
    source:StopClip()
    source:SetVolume(ui.clamp((volume or 1.0) * ui.volume, 0, 1))
    source:PlayClip()
    source:SetPitch(pitch or 1.0)
end

--------------------------------------------------------------------------------
-- chrome
--------------------------------------------------------------------------------

-- dark glass with a lit top edge
function ui.panel(x, y, w, h, s, a, strength)
    local k = strength or 0.78
    Hud.Rect(x, y, w, h, ui.ink(k * a))
    Hud.RectGradient(x, y, w, 90 * s, ui.white(0.035 * a), ui.white(0.035 * a), ui.white(0.0), ui.white(0.0))
    Hud.Rect(x, y, w, 1 * s, ui.white(0.16 * a))
end

-- a row of a list: glass when idle, solid white when selected, with a blue edge that grows in
function ui.list_bar(x, y, w, h, s, k, a)
    Hud.Rect(x, y, w, h, ui.ink(0.46 * a))
    Hud.Rect(x, y + h - 1 * s, w, 1 * s, ui.white(0.07 * a))
    if k > 0.004 then
        Hud.Rect(x, y, w, h, ui.white(0.94 * k * a))
        Hud.Rect(x, y, 4 * s, h, ui.accent(k * a))
    end
end

function ui.hairline(x, y, w, s, a)
    Hud.Rect(x, y, w, 1 * s, ui.white(0.14 * a))
end

-- the top bar: a mark, a breadcrumb trail on the left, label and value pairs on the right
function ui.header(w, s, a, crumbs, values, slide)
    local h = ui.header_height * s
    local y = -(slide or 0) * h
    Hud.RectGradient(0, y, w, h, ui.ink(0.88 * a), ui.ink(0.88 * a), ui.ink(0.74 * a), ui.ink(0.74 * a))
    Hud.Rect(0, y + h - 1 * s, w, 1 * s, ui.white(0.10 * a))

    local x = ui.margin * s
    Hud.Rect(x, y + h * 0.5 - 6 * s, 12 * s, 12 * s, ui.accent(a))
    x = x + 30 * s
    for i, crumb in ipairs(crumbs) do
        if i > 1 then
            Hud.Text("\xE2\x80\xBA", x, y + h * 0.5 - 13 * s, 20 * s, ui.white(0.35 * a), "plan_regular")
            x = x + 22 * s
        end
        local color = i == #crumbs and ui.white(a) or ui.white(0.5 * a)
        x = x + ui.label(crumb, x, y + h * 0.5 - 8 * s, 14 * s, color) + 20 * s
    end

    local rx = w - ui.margin * s
    for i = #values, 1, -1 do
        local entry = values[i]
        local vw = Hud.Text(entry[2], rx, y + h * 0.5 - 14 * s, 24 * s, ui.white(0.95 * a), entry[3] or "plan_thin", 1, 0.5 * s)
        ui.label(entry[1], rx - vw - 12 * s, y + h * 0.5 - 6 * s, 11 * s, ui.white(0.45 * a), 1)
        rx = rx - vw - 12 * s - ui.label_width(entry[1], 11 * s) - 44 * s
    end
end

-- a key pill: white with ink text, the prompts of a console menu
function ui.key_pill(text, x, y, s, a)
    local size = 12 * s
    local tw   = Hud.TextWidth(text, size, "plan_strong", 1 * s)
    local pw   = math.max(tw + 16 * s, 26 * s)
    Hud.Rect(x, y, pw, 24 * s, ui.white(0.9 * a), 12 * s)
    Hud.Text(text, x + pw * 0.5, y + 5 * s, size, ui.ink(a), "plan_strong", 0.5, 1 * s)
    return pw
end

-- the bottom bar, prompts right aligned in reading order, an optional draw callback on the left
function ui.footer(w, h, s, a, prompts, left, slide)
    local fh = ui.footer_height * s
    local y  = h - fh + (slide or 0) * fh
    Hud.RectGradient(0, y, w, fh, ui.ink(0.74 * a), ui.ink(0.74 * a), ui.ink(0.88 * a), ui.ink(0.88 * a))
    Hud.Rect(0, y, w, 1 * s, ui.white(0.10 * a))

    local total = 0
    for _, prompt in ipairs(prompts) do
        local tw = Hud.TextWidth(prompt[1], 12 * s, "plan_strong", 1 * s)
        total = total + math.max(tw + 16 * s, 26 * s) + 10 * s + ui.label_width(prompt[2], 12 * s) + 34 * s
    end
    local x = w - ui.margin * s - total + 34 * s
    for _, prompt in ipairs(prompts) do
        x = x + ui.key_pill(prompt[1], x, y + fh * 0.5 - 12 * s, s, a) + 10 * s
        x = x + ui.label(prompt[2], x, y + fh * 0.5 - 7 * s, 12 * s, ui.white(0.75 * a)) + 34 * s
    end

    if left then
        left(ui.margin * s, y, fh)
    end
end

-- a short message over the footer, blue edge when it went well, red when it did not
function ui.toast(w, h, s, a, text, ok, k)
    local size = 13 * s
    local tw   = ui.label_width(text, size)
    local bw   = tw + 64 * s
    local x    = w * 0.5 - bw * 0.5
    local y    = h - ui.footer_height * s - 74 * s + (1 - k) * 12 * s
    Hud.Rect(x, y, bw, 44 * s, ui.ink(0.92 * k * a))
    Hud.Rect(x, y, bw, 1 * s, ui.white(0.14 * k * a))
    Hud.Rect(x, y, 4 * s, 44 * s, ok and ui.accent(k * a) or ui.bad(k * a))
    ui.label(text, w * 0.5 + 2 * s, y + 15 * s, size, ui.white(k * a), 0.5)
end

return ui
