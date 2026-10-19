-- Cursor bridge: runs inside REAPER as a deferred script.
-- Requests arrive as <id>.lua files in <resource>/Scripts/cursor_bridge/queue, results are written as <id>.json.

local script_dir = debug.getinfo(1, "S").source:match("^@(.*[\\/])") or ""
local queue_dir = reaper.GetResourcePath() .. "/Scripts/cursor_bridge/queue"
local api_path = script_dir .. "cursor_bridge_api.lua"
local instance_token = tostring(reaper.time_precise()) .. "_" .. tostring(math.random(1, 1 << 30))

reaper.RecursiveCreateDirectory(queue_dir, 0)
reaper.SetExtState("cursor_bridge", "instance", instance_token, false)

local api = nil
local api_error = nil

local function load_api()
    local ok, result = pcall(dofile, api_path)
    if ok
    then
        api = result
        api_error = nil
    else
        api_error = tostring(result)
    end
    return ok, result
end

load_api()

local function write_file_atomic(path, text)
    local tmp_path = path .. ".tmp"
    local file = io.open(tmp_path, "wb")
    if not file
    then
        return false
    end
    file:write(text)
    file:close()
    os.remove(path)
    return os.rename(tmp_path, path)
end

local function read_file(path)
    local file = io.open(path, "rb")
    if not file
    then
        return nil
    end
    local text = file:read("a")
    file:close()
    return text
end

local function fallback_json_string(s)
    return '"' .. tostring(s):gsub('[%c"\\]', function(c) return string.format("\\u%04x", c:byte()) end) .. '"'
end

local function handle_request(id, code)
    local encode = api and api.json_encode or nil
    local env = setmetatable({}, { __index = _G })
    env.bridge = api
    env.reload_api = function()
        local ok, err = load_api()
        if not ok
        then
            error(err)
        end
        return { reloaded = true }
    end

    local response
    if not api
    then
        response = '{"ok":false,"error":' .. fallback_json_string("bridge api failed to load: " .. tostring(api_error)) .. '}'
    else
        local chunk, compile_error = load(code, "=request", "t", env)
        if not chunk
        then
            response = encode({ ok = false, error = "compile error: " .. tostring(compile_error) })
        else
            reaper.Undo_BeginBlock()
            reaper.PreventUIRefresh(1)
            local ok, result = xpcall(chunk, debug.traceback)
            reaper.PreventUIRefresh(-1)
            reaper.UpdateArrange()
            reaper.Undo_EndBlock("Cursor bridge", -1)
            local enc_ok, text = pcall(encode, ok and { ok = true, result = result } or { ok = false, error = tostring(result) })
            if enc_ok
            then
                response = text
            else
                response = '{"ok":false,"error":' .. fallback_json_string("result encode failed: " .. tostring(text)) .. '}'
            end
        end
    end

    write_file_atomic(queue_dir .. "/" .. id .. ".json", response)
end

local function poll()
    if reaper.GetExtState("cursor_bridge", "instance") ~= instance_token
    then
        return
    end

    reaper.EnumerateFiles(queue_dir, -1)
    local pending = {}
    local index = 0
    while true
    do
        local name = reaper.EnumerateFiles(queue_dir, index)
        if not name
        then
            break
        end
        local id = name:match("^(.+)%.lua$")
        if id
        then
            pending[#pending + 1] = id
        end
        index = index + 1
    end
    table.sort(pending)

    for _, id in ipairs(pending)
    do
        local request_path = queue_dir .. "/" .. id .. ".lua"
        local code = read_file(request_path)
        os.remove(request_path)
        if code
        then
            handle_request(id, code)
        end
    end

    reaper.defer(poll)
end

reaper.atexit(function()
    if reaper.GetExtState("cursor_bridge", "instance") == instance_token
    then
        reaper.DeleteExtState("cursor_bridge", "instance", false)
    end
end)

poll()
