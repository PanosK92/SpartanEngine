-- Copyright(c) 2015-2026 Panos Karabelas
-- Licensed under the Spartan Engine License. See license.md in the repository root.
-- https://github.com/PanosK92/SpartanEngine/blob/master/license.md
-- Commercial use requires written permission and negotiated payment terms.

local ui = {}

local function env_truthy(name)
    local v = os.getenv(name)
    return v ~= nil and v ~= "" and v ~= "0" and v:lower() ~= "false"
end

local is_ci   = env_truthy("CI") or env_truthy("GITHUB_ACTIONS") or env_truthy("TF_BUILD") or env_truthy("JENKINS_URL") or env_truthy("BUILDKITE")
local no_ansi = is_ci or env_truthy("NO_COLOR") or env_truthy("SPARTAN_PLAIN") or (os.getenv("TERM") == "dumb")

ui.plain   = no_ansi
ui.color   = not no_ansi
ui.unicode = not no_ansi
ui.width   = 74

-- palette matching spartan's electric cyan, emerald, amber and coral editor theme
local ANSI =
{
    reset      = "\27[0m",
    bold       = "\27[1m",
    dim        = "\27[2m",
    accent     = "\27[96m",
    accent_dim = "\27[36m",
    ok         = "\27[92m",
    warn       = "\27[93m",
    err        = "\27[91m",
    text       = "\27[97m",
    muted      = "\27[90m",
    clear_line = "\27[2K",
}

local GLYPH = ui.unicode and
{
    tl       = "╭",
    tr       = "╮",
    bl       = "╰",
    br       = "╯",
    h        = "─",
    v        = "│",
    ok       = "✓",
    cached   = "●",
    skip     = "–",
    warn     = "!",
    err      = "✗",
    arrow    = "▸",
    dot      = "·",
    bar_fill = "█",
    bar_half = "▓",
    bar_rest = "░",
    pip_fill = "━",
    pip_rest = "─",
} or
{
    tl       = "+",
    tr       = "+",
    bl       = "+",
    br       = "+",
    h        = "-",
    v        = "|",
    ok       = "[OK]",
    cached   = "[==]",
    skip     = "[--]",
    warn     = "[!!]",
    err      = "[XX]",
    arrow    = "[>>]",
    dot      = "-",
    bar_fill = "#",
    bar_half = "=",
    bar_rest = ".",
    pip_fill = "=",
    pip_rest = "-",
}

local function c(code, text)
    if not ui.color then
        return text
    end
    return code .. text .. ANSI.reset
end

local function utf8_len(s)
    -- strip ansi escape sequences before counting utf-8 codepoints
    local clean = s:gsub("\27%[[%d;]*[A-Za-z]", "")
    local count = 0
    for _ in clean:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
        count = count + 1
    end
    return count
end

local function pad_right(s, width)
    local len = utf8_len(s)
    if len >= width then
        return s
    end
    return s .. string.rep(" ", width - len)
end

local function repeat_str(s, n)
    if n <= 0 then
        return ""
    end
    return string.rep(s, n)
end

function ui.format_bytes(bytes)
    if not bytes or bytes < 0 then
        return "0 B"
    end
    if bytes < 1024 then
        return string.format("%d B", bytes)
    elseif bytes < 1024 * 1024 then
        return string.format("%.1f KB", bytes / 1024)
    elseif bytes < 1024 * 1024 * 1024 then
        return string.format("%.1f MB", bytes / (1024 * 1024))
    else
        return string.format("%.2f GB", bytes / (1024 * 1024 * 1024))
    end
end

function ui.format_duration(seconds)
    if not seconds or seconds < 0 then
        return "0ms"
    end
    if seconds < 1.0 then
        return string.format("%dms", math.floor(seconds * 1000 + 0.5))
    elseif seconds < 60.0 then
        return string.format("%.1fs", seconds)
    else
        local mins = math.floor(seconds / 60)
        local secs = math.floor(seconds - mins * 60 + 0.5)
        return string.format("%dm %02ds", mins, secs)
    end
end

function ui.format_bar(percent, width)
    width   = width or 28
    percent = math.max(0, math.min(100, percent or 0))
    local filled = math.floor((percent / 100) * width + 0.5)
    local empty  = width - filled
    local bar_filled = c(ANSI.accent, repeat_str(GLYPH.bar_fill, filled))
    local bar_empty  = c(ANSI.muted, repeat_str(GLYPH.bar_rest, empty))
    if ui.unicode then
        return bar_filled .. bar_empty
    end
    return "[" .. bar_filled .. bar_empty .. "]"
end

local function format_pipeline_bar(step_index, step_total, width)
    width = width or 14
    local frac   = step_total > 0 and (step_index / step_total) or 1
    local filled = math.floor(frac * width + 0.5)
    local empty  = width - filled
    return c(ANSI.accent, repeat_str(GLYPH.pip_fill, filled)) .. c(ANSI.muted, repeat_str(GLYPH.pip_rest, empty))
end

local function write_line(text)
    io.write(text .. "\n")
    io.flush()
end

local function box_top(border_color)
    write_line("  " .. c(border_color, GLYPH.tl .. repeat_str(GLYPH.h, ui.width - 2) .. GLYPH.tr))
end

local function box_bottom(border_color)
    write_line("  " .. c(border_color, GLYPH.bl .. repeat_str(GLYPH.h, ui.width - 2) .. GLYPH.br))
end

local function box_divider(border_color)
    if ui.unicode then
        write_line("  " .. c(border_color, "├" .. repeat_str(GLYPH.h, ui.width - 2) .. "┤"))
    else
        write_line("  " .. c(border_color, "+" .. repeat_str(GLYPH.h, ui.width - 2) .. "+"))
    end
end

local function box_row(border_color, content)
    local inner_w = ui.width - 4
    local padded  = pad_right(content, inner_w)
    write_line("  " .. c(border_color, GLYPH.v) .. " " .. padded .. " " .. c(border_color, GLYPH.v))
end

function ui.banner(title, subtitle, meta_rows)
    write_line("")
    box_top(ANSI.accent_dim)
    local header = c(ANSI.bold .. ANSI.accent, title)
    if subtitle and subtitle ~= "" then
        header = header .. "  " .. c(ANSI.muted, GLYPH.dot) .. "  " .. c(ANSI.text, subtitle)
    end
    box_row(ANSI.accent_dim, " " .. header)
    if meta_rows and #meta_rows > 0 then
        box_divider(ANSI.accent_dim)
        for _, row in ipairs(meta_rows) do
            local key   = c(ANSI.muted, pad_right(row[1], 13))
            local value = c(row[3] or ANSI.text, row[2])
            box_row(ANSI.accent_dim, " " .. key .. value)
        end
    end
    box_bottom(ANSI.accent_dim)
end

local active_step_start = nil

function ui.step_begin(index, total, title)
    active_step_start = os.clock()
    write_line("")
    local badge = c(ANSI.bold .. ANSI.accent, string.format("[%d/%d]", index, total))
    local label = c(ANSI.bold .. ANSI.text, title)
    local left  = "  " .. badge .. " " .. label

    local pct = total > 0 and math.floor((index / total) * 100 + 0.5) or 100
    local right = format_pipeline_bar(index, total, 14) .. " " .. c(ANSI.muted, string.format("%3d%%", pct))

    local gap = ui.width - utf8_len(left) - utf8_len(right) + 2
    local rule
    if gap >= 4 then
        rule = " " .. c(ANSI.muted, repeat_str(GLYPH.h, gap - 2)) .. " "
    else
        rule = "  "
    end
    write_line(left .. rule .. right)
end

function ui.section(title)
    write_line("")
    local left = "  " .. c(ANSI.bold .. ANSI.accent, GLYPH.arrow) .. " " .. c(ANSI.bold .. ANSI.text, title)
    local gap  = ui.width - utf8_len(left) + 1
    local rule = gap > 2 and (" " .. c(ANSI.muted, repeat_str(GLYPH.h, gap))) or ""
    write_line(left .. rule)
end

local LABEL_COL = 30

local function status_row(glyph, glyph_color, label, detail, detail_color)
    local icon = c(glyph_color, glyph)
    local name = c(ANSI.text, pad_right(label, LABEL_COL))
    local info = detail and detail ~= "" and c(detail_color or ANSI.muted, detail) or ""
    write_line("    " .. icon .. "  " .. name .. info)
end

function ui.item_ok(label, detail)
    status_row(GLYPH.ok, ANSI.ok, label, detail, ANSI.muted)
end

function ui.item_cached(label, detail)
    local text = detail and detail ~= "" and ("cached " .. GLYPH.dot .. " " .. detail) or "cached"
    status_row(GLYPH.cached, ANSI.accent_dim, label, text, ANSI.muted)
end

function ui.item_skip(label, detail)
    local text = detail and detail ~= "" and ("skipped " .. GLYPH.dot .. " " .. detail) or "skipped"
    status_row(GLYPH.skip, ANSI.muted, label, text, ANSI.muted)
end

function ui.item_warn(label, detail)
    status_row(GLYPH.warn, ANSI.warn, label, detail, ANSI.warn)
end

function ui.item_err(label, detail)
    status_row(GLYPH.err, ANSI.err, label, detail, ANSI.err)
end

function ui.item_info(label, detail)
    status_row(GLYPH.arrow, ANSI.accent, label, detail, ANSI.muted)
end

-- live inline status (overwritten cleanly once completed on interactive terminals)
function ui.activity_start(label, detail)
    local line = "    " .. c(ANSI.accent, GLYPH.arrow) .. "  " .. c(ANSI.text, label)
    if detail and detail ~= "" then
        line = line .. " " .. c(ANSI.muted, detail)
    end
    if ui.plain then
        write_line(line)
    else
        io.write("\r" .. ANSI.clear_line .. line)
        io.flush()
    end
end

function ui.activity_clear()
    if not ui.plain then
        io.write("\r" .. ANSI.clear_line)
        io.flush()
    end
end

function ui.activity_done(kind, label, detail)
    ui.activity_clear()
    if kind == "cached" then
        ui.item_cached(label, detail)
    elseif kind == "warn" then
        ui.item_warn(label, detail)
    elseif kind == "skip" then
        ui.item_skip(label, detail)
    elseif kind == "err" then
        ui.item_err(label, detail)
    else
        ui.item_ok(label, detail)
    end
end

-- live single-line progress bar (interactive) or milestone log (CI/plain)
function ui.create_progress(label)
    local state =
    {
        label          = label,
        start_time     = os.clock(),
        last_draw_time = 0,
        last_percent   = -1,
        last_milestone = -1,
        shown          = false,
    }

    function state:update(percent, detail)
        percent = math.max(0, math.min(100, math.floor((percent or 0) + 0.5)))
        local now = os.clock()

        if ui.plain then
            local milestone = math.floor(percent / 25) * 25
            if milestone > self.last_milestone or (percent == 100 and self.last_percent ~= 100) then
                self.last_milestone = milestone
                self.last_percent   = percent
                local suffix = detail and detail ~= "" and ("  " .. detail) or ""
                write_line(string.format("    %s  %-26s %s %3d%%%s", GLYPH.arrow, self.label, ui.format_bar(percent, 22), percent, suffix))
            end
            return
        end

        if percent == self.last_percent and (now - self.last_draw_time) < 0.08 and percent < 100 then
            return
        end
        self.last_percent   = percent
        self.last_draw_time = now
        self.shown          = true

        local bar    = ui.format_bar(percent, 24)
        local pct    = c(ANSI.bold .. ANSI.text, string.format("%3d%%", percent))
        local suffix = detail and detail ~= "" and ("  " .. c(ANSI.muted, detail)) or ""
        local line   = "    " .. c(ANSI.accent, GLYPH.arrow) .. "  " .. c(ANSI.text, pad_right(self.label, 22)) .. " " .. bar .. " " .. pct .. suffix
        io.write("\r" .. ANSI.clear_line .. line)
        io.flush()
    end

    function state:finish(detail, kind)
        if not ui.plain and self.shown then
            io.write("\r" .. ANSI.clear_line)
            io.flush()
        end
        local elapsed = ui.format_duration(os.clock() - self.start_time)
        local full_detail = detail and detail ~= "" and (detail .. " " .. GLYPH.dot .. " " .. elapsed) or elapsed
        ui.activity_done(kind or "ok", self.label, full_detail)
    end

    return state
end

function ui.summary_box(title, rows, is_error)
    local border = is_error and ANSI.err or ANSI.ok
    local icon   = is_error and GLYPH.err or GLYPH.ok
    write_line("")
    box_top(border)
    box_row(border, " " .. c(ANSI.bold .. border, icon .. "  " .. title))
    if rows and #rows > 0 then
        box_divider(border)
        for _, row in ipairs(rows) do
            local key   = c(ANSI.muted, pad_right(row[1], 13))
            local value = c(row[3] or ANSI.text, row[2])
            box_row(border, " " .. key .. value)
        end
    end
    box_bottom(border)
    write_line("")
end

ui.ansi  = ANSI
ui.glyph = GLYPH

return ui
