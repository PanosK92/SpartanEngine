--------------------------------------------------------------------------------
-- plan drive in
--
-- the cutscene that carries the player's car off a shop's forecourt and into its
-- spot inside. the shots and the drive itself live in the sequencer
-- (binaries/project/sequencer_<name>.xml, authored in the editor): the car is
-- driven by the sequencer's autopilot through the real simulation, so it rolls in
-- on its own wheels. this side starts it with the player's car, draws the skip
-- prompt and hands back once it ends or is skipped.
--
-- without a sequencer (a build without the editor, or a missing file) start()
-- returns nil and the caller places the car itself.
--------------------------------------------------------------------------------

local drive_in = {}
drive_in.__index = drive_in

local fade_in   = 0.6  -- seconds the opening shot takes to come up from the entrance's black
local skip_fade = 0.35 -- seconds a skip takes to dip to black
local skip_lock = 0.5  -- seconds before a skip is taken, so the key that started the trip cannot end it

local function pressed(...)
    for _, key in ipairs({ ... }) do
        if Input.GetKeyDown(key) then
            return true
        end
    end
    return false
end

function drive_in.start(ui, name, car)
    if not (Sequencer and Sequencer.IsAvailable() and Sequencer.Play(name, car)) then
        return nil
    end
    return setmetatable({ ui = ui, time = 0, skip_time = 0, skipping = false, active = true, skipped = false }, drive_in)
end

-- a key pill and its label in the bottom right corner, on the letterbox bar
local function draw_skip(ui, w, h, s, a)
    local label = "SKIP"
    local pw    = Hud.TextWidth("ENTER", 12 * s, "plan_strong", 1 * s) + 16 * s
    local lw    = ui.label_width(label, 12 * s)
    local x     = w - 56 * s - lw - 10 * s - pw
    local y     = h - 58 * s
    ui.key_pill("ENTER", x, y, s, a)
    ui.label(label, x + pw + 10 * s, y + 5 * s, 12 * s, ui.white(0.8 * a))
end

-- true while the trip runs. once it returns false the camera and the car are handed back
-- and self.skipped says whether the screen was left black
function drive_in:Tick(dt, w, h, s)
    if not self.active then
        return false
    end
    local ui  = self.ui
    self.time = self.time + dt

    if self.skipping then
        self.skip_time = self.skip_time + dt
        Hud.Rect(0, 0, w, h, ui.black(ui.smooth(self.skip_time / skip_fade)))
        if self.skip_time >= skip_fade + 0.05 then
            self:Finish(true)
            return false
        end
        return true
    end

    if not Sequencer.IsPlaying() then
        self:Finish(false)
        return false
    end

    Hud.Rect(0, 0, w, h, ui.black(1 - ui.smooth(self.time / fade_in)))
    draw_skip(ui, w, h, s, ui.smooth((self.time - 0.8) / 0.5))

    if self.time > skip_lock and pressed(KeyCode.Enter, KeyCode.Space, KeyCode.Esc, KeyCode.Button_South, KeyCode.Button_East, KeyCode.Start) then
        ui.sound("back")
        self.skipping = true
    end
    return true
end

function drive_in:Finish(skipped)
    if self.active then
        Sequencer.Stop()
        self.active  = false
        self.skipped = skipped
    end
end

return drive_in
