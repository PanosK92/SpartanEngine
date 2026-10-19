-- Helper API exposed to bridge requests as `bridge`. Reloadable with reload_api().

local api = {}

local template_dir = reaper.GetResourcePath() .. "/Scripts/cursor_bridge/templates"
reaper.RecursiveCreateDirectory(template_dir, 0)

-- json

local function json_string(s)
    local map = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
    return '"' .. s:gsub('[%c"\\]', function(c) return map[c] or string.format("\\u%04x", c:byte()) end) .. '"'
end

local function json_encode(value, depth)
    depth = depth or 0
    if depth > 64
    then
        error("json_encode: nesting too deep")
    end
    local kind = type(value)
    if value == nil
    then
        return "null"
    elseif kind == "boolean"
    then
        return tostring(value)
    elseif kind == "number"
    then
        if value ~= value or value == math.huge or value == -math.huge
        then
            return "null"
        end
        if math.type(value) == "integer"
        then
            return tostring(value)
        end
        return string.format("%.10g", value)
    elseif kind == "string"
    then
        return json_string(value)
    elseif kind == "table"
    then
        local count = #value
        local is_array = true
        for key in pairs(value)
        do
            if type(key) ~= "number" or key < 1 or key > count or key % 1 ~= 0
            then
                is_array = false
                break
            end
        end
        local parts = {}
        if is_array
        then
            for i = 1, count
            do
                parts[i] = json_encode(value[i], depth + 1)
            end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        for key, item in pairs(value)
        do
            parts[#parts + 1] = json_string(tostring(key)) .. ":" .. json_encode(item, depth + 1)
        end
        return "{" .. table.concat(parts, ",") .. "}"
    end
    return json_string(tostring(value))
end

api.json_encode = json_encode

-- tracks

local function db_from_gain(gain)
    if gain <= 0
    then
        return -150
    end
    return 20 * math.log(gain, 10)
end

local function gain_from_db(db)
    return 10 ^ (db / 20)
end

local function track_name(track)
    local _, name = reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "", false)
    return name
end

function api.resolve_track(ref)
    if ref == nil
    then
        error("track reference required")
    end
    if ref == "master"
    then
        return reaper.GetMasterTrack(0)
    end
    if type(ref) == "number"
    then
        local track = reaper.GetTrack(0, math.floor(ref))
        if not track
        then
            error("no track at index " .. tostring(ref))
        end
        return track
    end
    local wanted = tostring(ref):lower()
    for i = 0, reaper.CountTracks(0) - 1
    do
        local track = reaper.GetTrack(0, i)
        if track_name(track):lower() == wanted
        then
            return track
        end
    end
    error("no track named '" .. tostring(ref) .. "'")
end

local function fx_list(track)
    local list = {}
    for i = 0, reaper.TrackFX_GetCount(track) - 1
    do
        local _, name = reaper.TrackFX_GetFXName(track, i, "")
        local _, preset = reaper.TrackFX_GetPreset(track, i, "")
        list[#list + 1] = { index = i, name = name, enabled = reaper.TrackFX_GetEnabled(track, i), preset = preset }
    end
    return list
end

function api.track_info(track)
    local is_master = track == reaper.GetMasterTrack(0)
    local color = reaper.GetTrackColor(track)
    local info = {
        index = is_master and -1 or math.floor(reaper.GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER")) - 1,
        name = is_master and "master" or track_name(track),
        volume_db = db_from_gain(reaper.GetMediaTrackInfo_Value(track, "D_VOL")),
        pan = reaper.GetMediaTrackInfo_Value(track, "D_PAN"),
        mute = reaper.GetMediaTrackInfo_Value(track, "B_MUTE") == 1,
        solo = reaper.GetMediaTrackInfo_Value(track, "I_SOLO") ~= 0,
        folder_depth = math.floor(reaper.GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH")),
        item_count = reaper.CountTrackMediaItems(track),
        fx = fx_list(track),
    }
    if color ~= 0
    then
        local r, g, b = reaper.ColorFromNative(color)
        info.color = { r, g, b }
    end
    return info
end

local function apply_track_props(track, props)
    if props.name ~= nil
    then
        reaper.GetSetMediaTrackInfo_String(track, "P_NAME", props.name, true)
    end
    if props.volume_db ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "D_VOL", gain_from_db(props.volume_db))
    end
    if props.pan ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "D_PAN", props.pan)
    end
    if props.mute ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "B_MUTE", props.mute and 1 or 0)
    end
    if props.solo ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "I_SOLO", props.solo and 2 or 0)
    end
    if props.folder_depth ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "I_FOLDERDEPTH", props.folder_depth)
    end
    if props.color ~= nil
    then
        reaper.SetTrackColor(track, reaper.ColorToNative(props.color[1], props.color[2], props.color[3]))
    end
    if props.record_arm ~= nil
    then
        reaper.SetMediaTrackInfo_Value(track, "I_RECARM", props.record_arm and 1 or 0)
    end
    if props.record_input_midi_all ~= nil and props.record_input_midi_all
    then
        reaper.SetMediaTrackInfo_Value(track, "I_RECINPUT", 4096 + (63 << 5))
        reaper.SetMediaTrackInfo_Value(track, "I_RECMON", 1)
    end
end

function api.project_info()
    local num, den, tempo = reaper.TimeMap_GetTimeSigAtTime(0, 0)
    local tracks = {}
    for i = 0, reaper.CountTracks(0) - 1
    do
        tracks[#tracks + 1] = api.track_info(reaper.GetTrack(0, i))
    end
    local markers = {}
    local _, marker_count, region_count = reaper.CountProjectMarkers(0)
    for i = 0, marker_count + region_count - 1
    do
        local _, is_region, pos, region_end, name, number = reaper.EnumProjectMarkers(i)
        local entry = { name = name, number = number, is_region = is_region, qn = reaper.TimeMap2_timeToQN(0, pos) }
        if is_region
        then
            entry.end_qn = reaper.TimeMap2_timeToQN(0, region_end)
        end
        markers[#markers + 1] = entry
    end
    local _, project_path = reaper.EnumProjects(-1, "")
    local length = reaper.GetProjectLength(0)
    return {
        reaper_version = reaper.GetAppVersion(),
        project_path = project_path,
        tempo = tempo,
        time_signature = { num, den },
        sample_rate = reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 0, false),
        length_seconds = length,
        length_qn = reaper.TimeMap2_timeToQN(0, length),
        play_state = reaper.GetPlayState(),
        cursor_qn = reaper.TimeMap2_timeToQN(0, reaper.GetCursorPosition()),
        master = api.track_info(reaper.GetMasterTrack(0)),
        tracks = tracks,
        markers = markers,
    }
end

function api.track_create(props)
    props = props or {}
    local index = props.index or reaper.CountTracks(0)
    reaper.InsertTrackAtIndex(index, true)
    local track = reaper.GetTrack(0, index)
    apply_track_props(track, props)
    return api.track_info(track)
end

function api.track_update(ref, props)
    local track = api.resolve_track(ref)
    if props.delete
    then
        local info = api.track_info(track)
        reaper.DeleteTrack(track)
        return { deleted = info.name, index = info.index }
    end
    apply_track_props(track, props)
    return api.track_info(track)
end

function api.track_send(source_ref, dest_ref, volume_db, remove_master_send)
    local source = api.resolve_track(source_ref)
    local dest = api.resolve_track(dest_ref)
    local send_index = reaper.CreateTrackSend(source, dest)
    if volume_db ~= nil
    then
        reaper.SetTrackSendInfo_Value(source, 0, send_index, "D_VOL", gain_from_db(volume_db))
    end
    if remove_master_send
    then
        reaper.SetMediaTrackInfo_Value(source, "B_MAINSEND", 0)
    end
    return { send_index = send_index }
end

-- fx

function api.fx_installed(filter, limit)
    filter = filter and filter:lower() or nil
    limit = limit or 200
    local list = {}
    local index = 0
    while #list < limit
    do
        local ok, name, ident = reaper.EnumInstalledFX(index)
        if not ok
        then
            break
        end
        if not filter or name:lower():find(filter, 1, true) or ident:lower():find(filter, 1, true)
        then
            list[#list + 1] = { name = name, ident = ident }
        end
        index = index + 1
    end
    return list
end

function api.fx_add(ref, fx_name)
    local track = api.resolve_track(ref)
    local index = reaper.TrackFX_AddByName(track, fx_name, false, -1)
    if index < 0
    then
        error("could not add fx '" .. fx_name .. "'")
    end
    local _, name = reaper.TrackFX_GetFXName(track, index, "")
    return { index = index, name = name }
end

function api.fx_remove(ref, fx_index)
    local track = api.resolve_track(ref)
    return { removed = reaper.TrackFX_Delete(track, fx_index) }
end

function api.fx_params(ref, fx_index, filter, limit)
    local track = api.resolve_track(ref)
    filter = filter and filter:lower() or nil
    limit = limit or 400
    local list = {}
    local count = reaper.TrackFX_GetNumParams(track, fx_index)
    for p = 0, count - 1
    do
        local _, name = reaper.TrackFX_GetParamName(track, fx_index, p, "")
        if not filter or name:lower():find(filter, 1, true)
        then
            local _, formatted = reaper.TrackFX_GetFormattedParamValue(track, fx_index, p, "")
            list[#list + 1] = { index = p, name = name, value = reaper.TrackFX_GetParamNormalized(track, fx_index, p), formatted = formatted }
            if #list >= limit
            then
                break
            end
        end
    end
    return { count = count, params = list }
end

local function find_param(track, fx_index, key)
    if type(key) == "number"
    then
        return key
    end
    local wanted = tostring(key):lower()
    local partial = nil
    for p = 0, reaper.TrackFX_GetNumParams(track, fx_index) - 1
    do
        local _, name = reaper.TrackFX_GetParamName(track, fx_index, p, "")
        local lower = name:lower()
        if lower == wanted
        then
            return p
        end
        if not partial and lower:find(wanted, 1, true)
        then
            partial = p
        end
    end
    if partial
    then
        return partial
    end
    error("no parameter matching '" .. tostring(key) .. "'")
end

function api.fx_set_params(ref, fx_index, values)
    local track = api.resolve_track(ref)
    local results = {}
    for _, entry in ipairs(values)
    do
        local p = find_param(track, fx_index, entry.param)
        reaper.TrackFX_SetParamNormalized(track, fx_index, p, entry.value)
        local _, name = reaper.TrackFX_GetParamName(track, fx_index, p, "")
        local _, formatted = reaper.TrackFX_GetFormattedParamValue(track, fx_index, p, "")
        results[#results + 1] = { index = p, name = name, value = reaper.TrackFX_GetParamNormalized(track, fx_index, p), formatted = formatted }
    end
    return results
end

function api.fx_set_formatted(ref, fx_index, values)
    local track = api.resolve_track(ref)
    local results = {}
    for _, entry in ipairs(values)
    do
        local p = find_param(track, fx_index, entry.param)
        local function shown(v)
            local _, text = reaper.TrackFX_FormatParamValueNormalized(track, fx_index, p, v, "")
            if text == ""
            then
                reaper.TrackFX_SetParamNormalized(track, fx_index, p, v)
                _, text = reaper.TrackFX_GetFormattedParamValue(track, fx_index, p, "")
            end
            local number = tonumber((text:gsub("[^%d%.%-%+eE]", "")))
            if text:lower():find("inf")
            then
                number = text:find("%-") and -1e9 or 1e9
            end
            return number or 0
        end
        local lo, hi = 0.0, 1.0
        local increasing = shown(1.0) >= shown(0.0)
        for _ = 1, 40
        do
            local mid = (lo + hi) / 2
            if (shown(mid) < entry.value) == increasing
            then
                lo = mid
            else
                hi = mid
            end
        end
        reaper.TrackFX_SetParamNormalized(track, fx_index, p, (lo + hi) / 2)
        local _, name = reaper.TrackFX_GetParamName(track, fx_index, p, "")
        local _, formatted = reaper.TrackFX_GetFormattedParamValue(track, fx_index, p, "")
        results[#results + 1] = { name = name, target = entry.value, formatted = formatted }
    end
    return results
end

function api.fx_presets(ref, fx_index)
    local track = api.resolve_track(ref)
    local current, count = reaper.TrackFX_GetPresetIndex(track, fx_index)
    local _, name = reaper.TrackFX_GetPreset(track, fx_index, "")
    return { current_index = current, count = count, current_name = name }
end

function api.fx_set_preset(ref, fx_index, preset)
    local track = api.resolve_track(ref)
    local ok
    if type(preset) == "number"
    then
        ok = reaper.TrackFX_SetPresetByIndex(track, fx_index, preset)
    else
        ok = reaper.TrackFX_SetPreset(track, fx_index, preset)
    end
    local _, name = reaper.TrackFX_GetPreset(track, fx_index, "")
    return { ok = ok, current_name = name }
end

function api.fx_show(ref, fx_index, show)
    local track = api.resolve_track(ref)
    reaper.TrackFX_Show(track, fx_index, show and 3 or 2)
    return { shown = show }
end

function api.track_chunk_get(ref)
    local track = api.resolve_track(ref)
    local ok, chunk = reaper.GetTrackStateChunk(track, "", false)
    if not ok
    then
        error("could not read track chunk")
    end
    return chunk
end

function api.track_chunk_set(ref, chunk)
    local track = api.resolve_track(ref)
    if not reaper.SetTrackStateChunk(track, chunk, false)
    then
        error("REAPER rejected the track chunk")
    end
    return api.track_info(track)
end

-- track templates (fx chains + settings, items stripped)

local function strip_chunk(chunk)
    local out = {}
    local skip_depth = 0
    for line in chunk:gmatch("[^\r\n]+")
    do
        local trimmed = line:match("^%s*(.-)%s*$")
        if skip_depth > 0
        then
            if trimmed:sub(1, 1) == "<"
            then
                skip_depth = skip_depth + 1
            elseif trimmed == ">"
            then
                skip_depth = skip_depth - 1
            end
        elseif trimmed:match("^<ITEM")
        then
            skip_depth = 1
        elseif not trimmed:match("^TRACKID ") and not trimmed:match("^FXID ") and not trimmed:match("^GUID ")
        then
            out[#out + 1] = line
        end
    end
    return table.concat(out, "\n")
end

local function template_path(name)
    if not tostring(name):match("^[%w_%-]+$")
    then
        error("template names may only use letters, digits, '_' and '-'")
    end
    return template_dir .. "/" .. name .. ".chunk"
end

function api.template_save(ref, name)
    local track = api.resolve_track(ref)
    local ok, chunk = reaper.GetTrackStateChunk(track, "", false)
    if not ok
    then
        error("could not read track chunk")
    end
    local file = io.open(template_path(name), "wb")
    file:write(strip_chunk(chunk))
    file:close()
    return { saved = name, path = template_path(name) }
end

function api.template_list()
    local list = {}
    reaper.EnumerateFiles(template_dir, -1)
    local index = 0
    while true
    do
        local file = reaper.EnumerateFiles(template_dir, index)
        if not file
        then
            break
        end
        local name = file:match("^(.+)%.chunk$")
        if name
        then
            list[#list + 1] = name
        end
        index = index + 1
    end
    return list
end

function api.template_create_track(name, props)
    local file = io.open(template_path(name), "rb")
    if not file
    then
        error("no template named '" .. name .. "'")
    end
    local chunk = file:read("a")
    file:close()
    props = props or {}
    local index = props.index or reaper.CountTracks(0)
    reaper.InsertTrackAtIndex(index, true)
    local track = reaper.GetTrack(0, index)
    reaper.SetTrackStateChunk(track, chunk, false)
    apply_track_props(track, props)
    return api.track_info(track)
end

-- midi

local function items_in_range(track, start_time, end_time)
    local list = {}
    for i = 0, reaper.CountTrackMediaItems(track) - 1
    do
        local item = reaper.GetTrackMediaItem(track, i)
        local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
        local len = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
        if start_time == nil or (pos < end_time - 1e-9 and pos + len > start_time + 1e-9)
        then
            list[#list + 1] = item
        end
    end
    return list
end

function api.track_clear(ref, start_qn, end_qn)
    local track = api.resolve_track(ref)
    local start_time = start_qn and reaper.TimeMap2_QNToTime(0, start_qn) or nil
    local end_time = end_qn and reaper.TimeMap2_QNToTime(0, end_qn) or nil
    local items = items_in_range(track, start_time, end_time)
    for _, item in ipairs(items)
    do
        reaper.DeleteTrackMediaItem(track, item)
    end
    return { removed_items = #items }
end

function api.midi_write(ref, args)
    local track = api.resolve_track(ref)
    local start_qn = args.start_qn or 0
    local length_qn = args.length_qn
    local start_time = reaper.TimeMap2_QNToTime(0, start_qn)
    local end_time = reaper.TimeMap2_QNToTime(0, start_qn + length_qn)
    local removed = 0
    if args.replace
    then
        removed = api.track_clear(ref, start_qn, start_qn + length_qn).removed_items
    end
    local item = reaper.CreateNewMIDIItemInProj(track, start_time, end_time, false)
    local take = reaper.GetActiveTake(item)
    if args.name
    then
        reaper.GetSetMediaItemTakeInfo_String(take, "P_NAME", args.name, true)
    end
    local function ppq(qn)
        return reaper.MIDI_GetPPQPosFromProjQN(take, start_qn + qn)
    end
    for _, note in ipairs(args.notes or {})
    do
        local note_start = ppq(note.start)
        local note_end = ppq(note.start + note.length)
        reaper.MIDI_InsertNote(take, false, note.muted or false, note_start, note_end, note.channel or 0, note.pitch, note.velocity or 96, true)
    end
    for _, cc in ipairs(args.ccs or {})
    do
        if cc.pitch_bend ~= nil
        then
            local value = math.floor(cc.pitch_bend + 8192)
            reaper.MIDI_InsertCC(take, false, false, ppq(cc.qn), 0xE0, cc.channel or 0, value & 0x7F, (value >> 7) & 0x7F, true)
        else
            reaper.MIDI_InsertCC(take, false, false, ppq(cc.qn), 0xB0, cc.channel or 0, cc.cc, cc.value, true)
        end
    end
    reaper.MIDI_Sort(take)
    local _, note_count, cc_count = reaper.MIDI_CountEvts(take)
    return { removed_items = removed, notes = note_count, ccs = cc_count, start_qn = start_qn, end_qn = start_qn + length_qn }
end

function api.midi_read(ref, start_qn, end_qn, max_notes)
    local track = api.resolve_track(ref)
    max_notes = max_notes or 2000
    local start_time = start_qn and reaper.TimeMap2_QNToTime(0, start_qn) or nil
    local end_time = end_qn and reaper.TimeMap2_QNToTime(0, end_qn) or nil
    local notes = {}
    local items = {}
    for _, item in ipairs(items_in_range(track, start_time, end_time))
    do
        local take = reaper.GetActiveTake(item)
        local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
        local len = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
        items[#items + 1] = { start_qn = reaper.TimeMap2_timeToQN(0, pos), end_qn = reaper.TimeMap2_timeToQN(0, pos + len) }
        if take and reaper.TakeIsMIDI(take)
        then
            local _, note_count = reaper.MIDI_CountEvts(take)
            for n = 0, note_count - 1
            do
                local _, _, muted, ppq_start, ppq_end, channel, pitch, velocity = reaper.MIDI_GetNote(take, n)
                local qn_start = reaper.MIDI_GetProjQNFromPPQPos(take, ppq_start)
                if (not start_qn or qn_start >= start_qn - 1e-9) and (not end_qn or qn_start < end_qn - 1e-9) and #notes < max_notes
                then
                    notes[#notes + 1] = { start = qn_start, length = reaper.MIDI_GetProjQNFromPPQPos(take, ppq_end) - qn_start, pitch = pitch, velocity = velocity, channel = channel, muted = muted }
                end
            end
        end
    end
    return { items = items, notes = notes }
end

-- tempo, markers, transport

function api.tempo_set(bpm, num, den)
    if reaper.CountTempoTimeSigMarkers(0) > 0
    then
        reaper.SetTempoTimeSigMarker(0, 0, 0, -1, -1, bpm, num or 0, den or 0, false)
    else
        reaper.SetTempoTimeSigMarker(0, -1, 0, -1, -1, bpm, num or 0, den or 0, false)
    end
    reaper.UpdateTimeline()
    local n, d, tempo = reaper.TimeMap_GetTimeSigAtTime(0, 0)
    return { tempo = tempo, time_signature = { n, d } }
end

function api.markers_clear()
    local _, marker_count, region_count = reaper.CountProjectMarkers(0)
    for i = marker_count + region_count - 1, 0, -1
    do
        reaper.DeleteProjectMarkerByIndex(0, i)
    end
    return { removed = marker_count + region_count }
end

function api.marker_add(name, qn, end_qn)
    local pos = reaper.TimeMap2_QNToTime(0, qn)
    local is_region = end_qn ~= nil
    local region_end = is_region and reaper.TimeMap2_QNToTime(0, end_qn) or 0
    local index = reaper.AddProjectMarker2(0, is_region, pos, region_end, name, -1, 0)
    return { index = index }
end

function api.transport(action, qn)
    if qn ~= nil
    then
        reaper.SetEditCurPos(reaper.TimeMap2_QNToTime(0, qn), true, true)
    end
    local commands = { play = 1007, stop = 1016, pause = 1008 }
    if action and commands[action]
    then
        reaper.Main_OnCommand(commands[action], 0)
    end
    return { play_state = reaper.GetPlayState(), cursor_qn = reaper.TimeMap2_timeToQN(0, reaper.GetCursorPosition()) }
end

-- render / save

function api.render(args)
    local start_time = reaper.TimeMap2_QNToTime(0, args.start_qn or 0)
    local end_time = args.end_qn and reaper.TimeMap2_QNToTime(0, args.end_qn) or reaper.GetProjectLength(0)
    reaper.RecursiveCreateDirectory(args.directory, 0)
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", args.directory, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", args.file_name, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAA==", true)
    reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
    reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)
    reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", start_time, true)
    reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", end_time, true)
    reaper.GetSetProjectInfo(0, "RENDER_SRATE", args.sample_rate or 48000, true)
    reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
    reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", (args.tail_ms or 0) > 0 and 0xFF or 0, true)
    reaper.GetSetProjectInfo(0, "RENDER_TAILMS", args.tail_ms or 0, true)
    reaper.Main_OnCommand(42230, 0)
    local path = args.directory .. "/" .. args.file_name .. ".wav"
    local file = io.open(path, "rb")
    local size = nil
    if file
    then
        size = file:seek("end")
        file:close()
    end
    return { path = path, exists = size ~= nil, bytes = size, seconds = end_time - start_time }
end

function api.project_save(path)
    if path
    then
        reaper.Main_SaveProjectEx(0, path, 0)
    else
        reaper.Main_OnCommand(40026, 0)
    end
    local _, project_path = reaper.EnumProjects(-1, "")
    return { project_path = project_path }
end

function api.project_clear()
    for i = reaper.CountTracks(0) - 1, 0, -1
    do
        reaper.DeleteTrack(reaper.GetTrack(0, i))
    end
    api.markers_clear()
    return { cleared = true }
end

return api
