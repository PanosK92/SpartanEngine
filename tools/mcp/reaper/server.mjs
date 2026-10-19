#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawn, execFileSync } from "node:child_process";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { analyze_wav } from "./audio_analysis.mjs";
import { read_fx_xml, write_fx_xml } from "./plugin_state.mjs";
import { bbc_instruments, bbc_find_instrument } from "./spitfire_bbc.mjs";

const reaper_exe = process.env.REAPER_EXE ?? "C:/Program Files/REAPER (x64)/reaper.exe";
const resource_dir = process.env.REAPER_RESOURCE_DIR ?? path.join(process.env.APPDATA ?? os.homedir(), "REAPER");
const bridge_dir = path.join(resource_dir, "Scripts", "cursor_bridge");
const queue_dir = path.join(bridge_dir, "queue");
const start_script = path.join(bridge_dir, "start_cursor_bridge.lua");
const default_render_dir = process.env.REAPER_RENDER_DIR ?? path.join(os.homedir(), "Music", "spartan_theme", "renders");

let request_counter = 0;

// lua literal encoding

function lua_string(value)
{
    let out = "\"";
    for (const ch of value)
    {
        const code = ch.codePointAt(0);
        if (ch === "\\" || ch === "\"")
        {
            out += "\\" + ch;
        }
        else if (code < 32 || code === 127)
        {
            out += "\\" + String(code).padStart(3, "0");
        }
        else
        {
            out += ch;
        }
    }
    return out + "\"";
}

function to_lua(value)
{
    if (value === null || value === undefined)
    {
        return "nil";
    }
    if (typeof value === "boolean")
    {
        return value ? "true" : "false";
    }
    if (typeof value === "number")
    {
        if (!Number.isFinite(value))
        {
            throw new Error("non-finite number cannot be sent to REAPER");
        }
        return String(value);
    }
    if (typeof value === "string")
    {
        return lua_string(value);
    }
    if (Array.isArray(value))
    {
        return "{" + value.map(to_lua).join(",") + "}";
    }
    return "{" + Object.entries(value).filter(([, v]) => v !== undefined).map(([k, v]) => `[${lua_string(k)}]=${to_lua(v)}`).join(",") + "}";
}

function call(fn, ...args)
{
    return `return bridge.${fn}(${args.map(to_lua).join(", ")})`;
}

// bridge transport

function sleep(ms)
{
    return new Promise((resolve) => setTimeout(resolve, ms));
}

async function bridge_request(code, timeout_ms = 30000)
{
    fs.mkdirSync(queue_dir, { recursive: true });
    request_counter++;
    const id = `${Date.now().toString().padStart(15, "0")}_${String(request_counter).padStart(6, "0")}_${process.pid}`;
    const request_path = path.join(queue_dir, `${id}.lua`);
    const response_path = path.join(queue_dir, `${id}.json`);
    fs.writeFileSync(request_path + ".tmp", code, "utf8");
    fs.renameSync(request_path + ".tmp", request_path);

    const deadline = Date.now() + timeout_ms;
    while (Date.now() < deadline)
    {
        if (fs.existsSync(response_path))
        {
            const text = fs.readFileSync(response_path, "utf8");
            fs.rmSync(response_path, { force: true });
            const response = JSON.parse(text);
            if (!response.ok)
            {
                throw new Error(`REAPER: ${response.error}`);
            }
            return response.result;
        }
        await sleep(15);
    }

    const picked_up = !fs.existsSync(request_path);
    fs.rmSync(request_path, { force: true });
    throw new Error(picked_up
        ? `REAPER picked up the request but did not answer within ${timeout_ms} ms (it may be blocked by a modal dialog)`
        : "REAPER bridge is not responding. Use reaper_ensure to start REAPER or the bridge script.");
}

function reaper_running()
{
    try
    {
        const output = execFileSync("tasklist", ["/FI", "IMAGENAME eq reaper.exe", "/NH"], { encoding: "utf8", windowsHide: true });
        return /reaper\.exe/i.test(output);
    }
    catch
    {
        return false;
    }
}

async function ensure_bridge()
{
    try
    {
        return { started: false, info: await bridge_request("return { version = reaper.GetAppVersion() }", 1500) };
    }
    catch
    {
    }
    const running = reaper_running();
    const args = running ? ["-nonewinst", start_script] : [];
    spawn(reaper_exe, args, { detached: true, stdio: "ignore", windowsHide: false }).unref();
    const deadline = Date.now() + (running ? 10000 : 60000);
    while (Date.now() < deadline)
    {
        try
        {
            return { started: running ? "bridge" : "reaper", info: await bridge_request("return { version = reaper.GetAppVersion() }", 1500) };
        }
        catch
        {
            await sleep(500);
        }
    }
    throw new Error("REAPER did not bring the bridge up in time");
}

// note helpers

const note_offsets = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };

function parse_pitch(pitch)
{
    if (typeof pitch === "number")
    {
        return pitch;
    }
    const match = /^([A-Ga-g])(#|b|s)?(-?\d+)$/.exec(pitch.trim());
    if (!match)
    {
        throw new Error(`cannot parse pitch '${pitch}' (use MIDI numbers or names like C4, F#3, Bb2; C4 = 60)`);
    }
    const accidental = match[2] === "#" || match[2] === "s" ? 1 : match[2] === "b" ? -1 : 0;
    return (Number(match[3]) + 1) * 12 + note_offsets[match[1].toLowerCase()] + accidental;
}

function expand_ramps(ramps)
{
    const events = [];
    for (const ramp of ramps ?? [])
    {
        const step = ramp.step_qn ?? 0.125;
        const points = [...ramp.points].sort((a, b) => a[0] - b[0]);
        for (let i = 0; i < points.length; i++)
        {
            const [qn, value] = points[i];
            events.push({ qn, cc: ramp.cc, value: Math.round(value), channel: ramp.channel });
            if (i + 1 < points.length)
            {
                const [next_qn, next_value] = points[i + 1];
                for (let t = qn + step; t < next_qn - 1e-6; t += step)
                {
                    const f = (t - qn) / (next_qn - qn);
                    const shaped = ramp.curve === "ease" ? 0.5 - 0.5 * Math.cos(Math.PI * f) : f;
                    events.push({ qn: t, cc: ramp.cc, value: Math.round(value + (next_value - value) * shaped), channel: ramp.channel });
                }
            }
        }
    }
    return events;
}

// tool registration

const server = new McpServer({ name: "reaper", version: "0.1.0" });

function text_result(value)
{
    return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 2) }] };
}

function register(name, description, schema, handler)
{
    server.registerTool(name, { title: name.replaceAll("_", " "), description, inputSchema: schema }, async (args) =>
    {
        try
        {
            return text_result(await handler(args ?? {}));
        }
        catch (error)
        {
            return { isError: true, content: [{ type: "text", text: String(error?.message ?? error) }] };
        }
    });
}

const track_ref = z.union([z.number().int(), z.string()]).describe("Track index (0-based), exact track name, or 'master'.");
const rgb = z.array(z.number().int().min(0).max(255)).length(3);
const track_props = {
    name: z.string().optional(),
    volume_db: z.number().optional(),
    pan: z.number().min(-1).max(1).optional(),
    mute: z.boolean().optional(),
    solo: z.boolean().optional(),
    color: rgb.optional(),
    folder_depth: z.number().int().optional().describe("1 = folder start, -1 = closes one folder level, 0 = normal."),
    record_arm: z.boolean().optional(),
    record_input_midi_all: z.boolean().optional().describe("Set record input to all MIDI inputs and enable monitoring, so the user can play the instrument live."),
};

register("reaper_ensure", "Make sure REAPER is running with the Cursor bridge active. Starts REAPER (bridge auto-loads via __startup.lua) or injects the bridge into a running REAPER.", {}, async () => ensure_bridge());

register("reaper_project_info", "Tempo, time signature, tracks (with fx chains, volume, pan), markers, length and transport state of the current REAPER project.", {}, async () => bridge_request(call("project_info")));

register("reaper_execute_lua", "Run Lua inside REAPER (ReaScript API as `reaper`, bridge helpers as `bridge`). Must `return` a JSON-serialisable value. Each call is one undo step. Use for anything the typed tools do not cover.", {
    code: z.string(),
    timeout_ms: z.number().int().positive().optional(),
}, async ({ code, timeout_ms }) => bridge_request(code, timeout_ms));

register("reaper_reload_bridge", "Reload cursor_bridge_api.lua inside REAPER after editing it.", {}, async () => bridge_request("return reload_api()"));

register("reaper_track_create", "Create a track, optionally from a saved template (fx chain + instrument state).", {
    ...track_props,
    index: z.number().int().optional(),
    template: z.string().optional().describe("Template name saved with reaper_template_save."),
}, async ({ template, ...props }) => (template ? bridge_request(call("template_create_track", template, props)) : bridge_request(call("track_create", props))));

register("reaper_track_update", "Change track properties or delete a track.", {
    track: track_ref,
    ...track_props,
    delete: z.boolean().optional(),
}, async ({ track, ...props }) => bridge_request(call("track_update", track, props)));

register("reaper_track_send", "Create a send from one track to another (e.g. to a reverb bus).", {
    source: track_ref,
    destination: track_ref,
    volume_db: z.number().optional(),
    remove_master_send: z.boolean().optional(),
}, async ({ source, destination, volume_db, remove_master_send }) => bridge_request(call("track_send", source, destination, volume_db, remove_master_send)));

register("reaper_track_clear", "Delete media items on a track, optionally only those overlapping a quarter-note range.", {
    track: track_ref,
    start_qn: z.number().optional(),
    end_qn: z.number().optional(),
}, async ({ track, start_qn, end_qn }) => bridge_request(call("track_clear", track, start_qn, end_qn)));

register("reaper_template_save", "Save a track's fx chain and settings (items stripped) as a reusable template, e.g. after the user picks a patch inside a sampler UI.", {
    track: track_ref,
    name: z.string().regex(/^[\w-]+$/),
}, async ({ track, name }) => bridge_request(call("template_save", track, name)));

register("reaper_template_list", "List saved track templates.", {}, async () => bridge_request(call("template_list")));

register("reaper_fx_installed", "List installed plugins (VST/VST3/CLAP/JS), filtered by substring.", {
    filter: z.string().optional(),
    limit: z.number().int().positive().optional(),
}, async ({ filter, limit }) => bridge_request(call("fx_installed", filter, limit)));

register("reaper_fx_add", "Add a plugin to a track by name, e.g. 'VST3i: BBC Symphony Orchestra (Spitfire Audio)' or 'ReaEQ'.", {
    track: track_ref,
    fx_name: z.string(),
}, async ({ track, fx_name }) => bridge_request(call("fx_add", track, fx_name)));

register("reaper_fx_remove", "Remove a plugin from a track.", {
    track: track_ref,
    fx_index: z.number().int(),
}, async ({ track, fx_index }) => bridge_request(call("fx_remove", track, fx_index)));

register("reaper_fx_params", "List plugin parameters with normalized (0-1) and formatted values.", {
    track: track_ref,
    fx_index: z.number().int(),
    filter: z.string().optional(),
    limit: z.number().int().positive().optional(),
}, async ({ track, fx_index, filter, limit }) => bridge_request(call("fx_params", track, fx_index, filter, limit)));

register("reaper_fx_set_params", "Set plugin parameters by index or (partial) name to normalized 0-1 values. Returns the formatted result so you can check units.", {
    track: track_ref,
    fx_index: z.number().int(),
    values: z.array(z.object({ param: z.union([z.number().int(), z.string()]), value: z.number().min(0).max(1) })),
}, async ({ track, fx_index, values }) => bridge_request(call("fx_set_params", track, fx_index, values)));

register("reaper_fx_set_formatted", "Set plugin parameters to a target value in the plugin's own display units (dB, Hz, ms, ratio...), found by searching the normalized range. Use for ReaEQ/ReaComp/ReaLimit etc.", {
    track: track_ref,
    fx_index: z.number().int(),
    values: z.array(z.object({ param: z.union([z.number().int(), z.string()]), value: z.number() })),
}, async ({ track, fx_index, values }) => bridge_request(call("fx_set_formatted", track, fx_index, values)));

register("reaper_fx_preset", "Get the plugin's current preset, or set it by name or index.", {
    track: track_ref,
    fx_index: z.number().int(),
    preset: z.union([z.string(), z.number().int()]).optional(),
}, async ({ track, fx_index, preset }) => (preset === undefined ? bridge_request(call("fx_presets", track, fx_index)) : bridge_request(call("fx_set_preset", track, fx_index, preset))));

register("reaper_fx_show", "Open or close a plugin's floating window (so the user can pick a patch in its UI).", {
    track: track_ref,
    fx_index: z.number().int(),
    show: z.boolean(),
}, async ({ track, fx_index, show }) => bridge_request(call("fx_show", track, fx_index, show)));

register("reaper_fx_state_xml_get", "Read the XML state embedded in a JUCE-based plugin (e.g. Spitfire BBC Symphony Orchestra: instrument name, articulations, CC mappings, mic mix). Use `contains` to return only matching tag snippets.", {
    track: track_ref,
    fx_index: z.number().int(),
    contains: z.string().optional(),
}, async ({ track, fx_index, contains }) =>
{
    const state = read_fx_xml(await bridge_request(call("track_chunk_get", track)), fx_index);
    if (!contains)
    {
        return state;
    }
    const snippets = state.xml.split(/(?=<)/).filter((tag) => tag.includes(contains));
    return { header: state.header, capacity: state.capacity, matches: snippets.slice(0, 200) };
});

register("reaper_fx_state_xml_set", "Rewrite a JUCE plugin's embedded XML state, either wholesale (`xml`) or with literal find/replace pairs, then reload the track so the plugin re-reads it. The new XML must not be longer than the existing state.", {
    track: track_ref,
    fx_index: z.number().int(),
    xml: z.string().optional(),
    replacements: z.array(z.object({ find: z.string(), replace: z.string() })).optional(),
}, async ({ track, fx_index, xml, replacements }) =>
{
    const chunk = await bridge_request(call("track_chunk_get", track));
    let new_xml = xml ?? read_fx_xml(chunk, fx_index).xml;
    for (const { find, replace } of replacements ?? [])
    {
        if (!new_xml.includes(find))
        {
            throw new Error(`'${find}' not found in plugin XML`);
        }
        new_xml = new_xml.split(find).join(replace);
    }
    await bridge_request(call("track_chunk_set", track, write_fx_xml(chunk, fx_index, new_xml)), 60000);
    await sleep(1500);
    const reloaded = read_fx_xml(await bridge_request(call("track_chunk_get", track)), fx_index);
    return { written_bytes: Buffer.byteLength(new_xml), reloaded_meta: reloaded.xml.match(/<META[^>]*>/)?.[0] ?? null, reloaded_articulations: [...reloaded.xml.matchAll(/id="a_name" value="([^"]*)"/g)].map((m) => m[1]) };
});

const bbc_fx_name = "VST3i: BBC Symphony Orchestra (Spitfire Audio)";

function select_articulation(xml, articulation)
{
    const blocks = [...xml.matchAll(/<ARTIC>[\s\S]*?<\/ARTIC>/g)];
    const names = blocks.map((block) => block[0].match(/id="a_name" value="([^"]*)"/)?.[1] ?? "");
    let index = 0;
    if (articulation)
    {
        const wanted = articulation.toLowerCase();
        index = names.findIndex((name) => name.toLowerCase().endsWith(wanted));
        if (index < 0)
        {
            index = names.findIndex((name) => name.toLowerCase().includes(wanted));
        }
        if (index < 0)
        {
            throw new Error(`articulation '${articulation}' not found. Available: ${names.join(", ")}`);
        }
    }
    let block_number = 0;
    const updated = xml.replace(/<ARTIC>[\s\S]*?<\/ARTIC>/g, (block) =>
    {
        const active = block_number === index ? "2" : "0";
        block_number++;
        return block.replace(/id="a_active" value="[^"]*"/, `id="a_active" value="${active}"`);
    }).replace(/id="p_lastSelectedPrimaryArtic" value="[^"]*"/, `id="p_lastSelectedPrimaryArtic" value="${index}"`);
    return { xml: updated, articulation: names[index] };
}

register("reaper_bbc_instruments", "List the Spitfire BBC Symphony Orchestra Discover instruments (keys for reaper_bbc_track_create) and their articulations.", {}, async () =>
    bbc_instruments().map(({ key, name, family, articulations }) => ({ key, name, family, articulations: articulations.map((a) => a.name) })));

register("reaper_bbc_track_create", "Create a track playing one BBC Symphony Orchestra Discover instrument with one articulation (track-per-articulation template style). Loads the instrument state directly, no UI clicks needed, and verifies the plugin reloaded it.", {
    instrument: z.string().describe("Instrument key from reaper_bbc_instruments, e.g. strings_violins_1, brass_horns_a4, percussion_percussion."),
    articulation: z.string().optional().describe("Articulation name suffix, e.g. 'Long', 'Short Spiccato', 'Short Pizzicato', 'Tremolo', 'Short Staccatissimo', 'Hits'. Defaults to the first."),
    ...track_props,
    index: z.number().int().optional(),
    reverb: z.number().min(0).max(1).optional().describe("Plugin's built-in hall reverb amount."),
    release: z.number().min(0).max(1).optional(),
    tightness: z.number().min(0).max(1).optional().describe("Trims sample start for tighter timing; higher = tighter."),
}, async (args) => bbc_track_create(args));

async function bbc_track_create({ instrument, articulation, reverb, release, tightness, ...props })
{
    const source = bbc_find_instrument(instrument);
    const selected = select_articulation(source.xml, articulation);
    const track = await bridge_request(call("track_create", { name: props.name ?? source.name.replace("Discover - ", ""), ...props }));
    await bridge_request(call("fx_add", track.index, bbc_fx_name));
    const needed = Buffer.byteLength(selected.xml);
    const deadline = Date.now() + 45000;
    let chunk = null;
    while (true)
    {
        chunk = await bridge_request(call("track_chunk_get", track.index));
        let capacity = 0;
        try
        {
            capacity = read_fx_xml(chunk, 0).capacity;
        }
        catch
        {
        }
        if (capacity >= needed)
        {
            break;
        }
        if (Date.now() > deadline)
        {
            throw new Error(`BBC plugin state stayed at ${capacity} bytes; it never finished loading its default instrument`);
        }
        await sleep(500);
    }
    await bridge_request(call("track_chunk_set", track.index, write_fx_xml(chunk, 0, selected.xml)), 60000);
    const params = [];
    if (reverb !== undefined)
    {
        params.push({ param: "Reverb", value: reverb });
    }
    if (release !== undefined)
    {
        params.push({ param: "Release", value: release });
    }
    if (tightness !== undefined)
    {
        params.push({ param: "Tightness", value: tightness });
    }
    if (params.length > 0)
    {
        await bridge_request(call("fx_set_params", track.index, 0, params));
    }
    const reloaded = read_fx_xml(await bridge_request(call("track_chunk_get", track.index)), 0).xml;
    const active = [...reloaded.matchAll(/<ARTIC>[\s\S]*?<\/ARTIC>/g)].map((block) => ({ name: block[0].match(/id="a_name" value="([^"]*)"/)?.[1], active: block[0].match(/id="a_active" value="([^"]*)"/)?.[1] })).filter((a) => a.active !== "0");
    const loaded_name = reloaded.match(/<META[^>]* name="([^"]*)"/)?.[1];
    if (loaded_name !== source.name)
    {
        throw new Error(`plugin reloaded '${loaded_name}' instead of '${source.name}'`);
    }
    return { track: track.index, name: props.name ?? source.name.replace("Discover - ", ""), instrument: loaded_name, requested_articulation: selected.articulation, active_articulations: active };
}

function prepare_midi(entry)
{
    const notes = (entry.notes ?? []).map((note) => ({ ...note, pitch: parse_pitch(note.pitch) }));
    return { notes, ccs: [...(entry.ccs ?? []), ...expand_ramps(entry.cc_ramps)] };
}

register("reaper_score_apply", "Apply a JSON score file to the project: tempo, markers, and per track an optional instrument (BBC instrument/articulation, or a plain fx name), mix settings and MIDI. Missing tracks are created; existing tracks with the same name get their MIDI replaced. Score format: { tempo, time_signature:[n,d], markers:[{name,qn,end_qn}], tracks:[{ name, bbc:{instrument,articulation,reverb,release,tightness}, fx:[names], volume_db, pan, color, start_qn, length_qn, notes:[{pitch,start,length,velocity}], ccs:[], cc_ramps:[] }] }.", {
    path: z.string(),
    only_tracks: z.array(z.string()).optional().describe("Apply only these track names."),
    rebuild_tracks: z.boolean().optional().describe("Delete and recreate tracks that already exist (to reload instruments)."),
}, async ({ path: score_path, only_tracks, rebuild_tracks }) =>
{
    const score = JSON.parse(fs.readFileSync(score_path, "utf8"));
    const report = { tracks: [] };
    if (score.tempo)
    {
        report.tempo = await bridge_request(call("tempo_set", score.tempo, score.time_signature?.[0], score.time_signature?.[1]));
    }
    if (score.markers && !only_tracks)
    {
        const code = ["bridge.markers_clear()", ...score.markers.map((m) => `bridge.marker_add(${to_lua(m.name)}, ${to_lua(m.qn)}, ${to_lua(m.end_qn)})`), "return true"].join("\n");
        await bridge_request(code);
        report.markers = score.markers.length;
    }
    const existing = new Set((await bridge_request(call("project_info"))).tracks.map((t) => t.name));
    for (const [order, entry] of score.tracks.entries())
    {
        if (only_tracks && !only_tracks.includes(entry.name))
        {
            continue;
        }
        const props = { name: entry.name, volume_db: entry.volume_db, pan: entry.pan, color: entry.color, folder_depth: entry.folder_depth };
        if (existing.has(entry.name) && rebuild_tracks)
        {
            await bridge_request(call("track_update", entry.name, { delete: true }));
            existing.delete(entry.name);
        }
        const created = !existing.has(entry.name);
        if (created)
        {
            if (entry.bbc)
            {
                await bbc_track_create({ ...entry.bbc, ...props });
            }
            else
            {
                await bridge_request(call("track_create", props));
                for (const fx_name of entry.fx ?? [])
                {
                    await bridge_request(call("fx_add", entry.name, fx_name));
                }
            }
            existing.add(entry.name);
        }
        else
        {
            await bridge_request(call("track_update", entry.name, props));
        }
        let midi = null;
        if (entry.notes || entry.ccs || entry.cc_ramps)
        {
            const { notes, ccs } = prepare_midi(entry);
            midi = await bridge_request(call("track_clear", entry.name), 30000).then(() => bridge_request(call("midi_write", entry.name, { start_qn: entry.start_qn ?? 0, length_qn: entry.length_qn ?? score.length_qn, notes, ccs, name: entry.name }), 120000));
        }
        report.tracks.push({ order, name: entry.name, created, notes: midi?.notes ?? 0, ccs: midi?.ccs ?? 0 });
    }
    return report;
});

const note_schema = z.object({
    pitch: z.union([z.number().int().min(0).max(127), z.string()]).describe("MIDI number or name (C4 = 60)."),
    start: z.number().min(0).describe("Quarter notes from the item start."),
    length: z.number().positive().describe("Quarter notes."),
    velocity: z.number().int().min(1).max(127).optional(),
    channel: z.number().int().min(0).max(15).optional(),
});

register("reaper_midi_write", "Write a MIDI item: notes, CC points and smooth CC ramps (e.g. CC1 dynamics / CC11 expression for orchestral samplers, keyswitch notes). Positions are in quarter notes relative to start_qn.", {
    track: track_ref,
    start_qn: z.number().min(0),
    length_qn: z.number().positive(),
    notes: z.array(note_schema).optional(),
    ccs: z.array(z.object({ qn: z.number().min(0), cc: z.number().int().min(0).max(127).optional(), value: z.number().int().min(0).max(127).optional(), pitch_bend: z.number().int().min(-8192).max(8191).optional(), channel: z.number().int().min(0).max(15).optional() })).optional(),
    cc_ramps: z.array(z.object({ cc: z.number().int().min(0).max(127), points: z.array(z.tuple([z.number(), z.number().min(0).max(127)])).min(1), step_qn: z.number().positive().optional(), curve: z.enum(["linear", "ease"]).optional(), channel: z.number().int().min(0).max(15).optional() })).optional(),
    replace: z.boolean().optional().describe("Delete existing items overlapping the range first."),
    name: z.string().optional(),
}, async ({ track, start_qn, length_qn, notes, ccs, cc_ramps, replace, name }) =>
{
    const parsed_notes = (notes ?? []).map((note) => ({ ...note, pitch: parse_pitch(note.pitch) }));
    const all_ccs = [...(ccs ?? []), ...expand_ramps(cc_ramps)];
    return bridge_request(call("midi_write", track, { start_qn, length_qn, notes: parsed_notes, ccs: all_ccs, replace, name }), 60000);
});

register("reaper_midi_read", "Read notes back from a track (positions in project quarter notes).", {
    track: track_ref,
    start_qn: z.number().optional(),
    end_qn: z.number().optional(),
    max_notes: z.number().int().positive().optional(),
}, async ({ track, start_qn, end_qn, max_notes }) => bridge_request(call("midi_read", track, start_qn, end_qn, max_notes)));

register("reaper_tempo_set", "Set the project tempo and time signature.", {
    bpm: z.number().positive(),
    numerator: z.number().int().positive().optional(),
    denominator: z.number().int().positive().optional(),
}, async ({ bpm, numerator, denominator }) => bridge_request(call("tempo_set", bpm, numerator, denominator)));

register("reaper_markers_set", "Add markers or regions (song sections), optionally clearing existing ones.", {
    clear: z.boolean().optional(),
    markers: z.array(z.object({ name: z.string(), qn: z.number().min(0), end_qn: z.number().optional().describe("Set to make a region.") })),
}, async ({ clear, markers }) =>
{
    const code = [clear ? "bridge.markers_clear()" : "", ...markers.map((m) => `bridge.marker_add(${to_lua(m.name)}, ${to_lua(m.qn)}, ${to_lua(m.end_qn)})`), "return bridge.project_info().markers"].join("\n");
    return bridge_request(code);
});

register("reaper_transport", "Play, stop or pause, and/or move the edit cursor (quarter notes).", {
    action: z.enum(["play", "stop", "pause"]).optional(),
    qn: z.number().min(0).optional(),
}, async ({ action, qn }) => bridge_request(call("transport", action, qn)));

register("reaper_render", "Render the master mix (24-bit WAV) of a quarter-note range, then analyze it: integrated LUFS, peak, clipping, stereo correlation, band balance, per-second loudness timeline and a spectrogram PNG you can Read to inspect.", {
    file_name: z.string().regex(/^[\w-]+$/),
    directory: z.string().optional(),
    start_qn: z.number().min(0).optional(),
    end_qn: z.number().positive().optional(),
    tail_ms: z.number().int().min(0).optional().describe("Extra time rendered for reverb tails."),
    sample_rate: z.number().int().optional(),
    analyze: z.boolean().optional(),
}, async ({ file_name, directory, start_qn, end_qn, tail_ms, sample_rate, analyze }) =>
{
    const dir = (directory ?? default_render_dir).replaceAll("\\", "/");
    fs.mkdirSync(dir, { recursive: true });
    const result = await bridge_request(call("render", { directory: dir, file_name, start_qn, end_qn, tail_ms: tail_ms ?? 2000, sample_rate }), 600000);
    if (!result.exists)
    {
        throw new Error(`render did not produce ${result.path}`);
    }
    return analyze === false ? result : { render: result, analysis: analyze_wav(result.path) };
});

register("audio_analyze", "Analyze any WAV file: loudness, peaks, clipping, band balance, loudness timeline and spectrogram PNG.", {
    path: z.string(),
    spectrogram: z.boolean().optional(),
    timeline_seconds: z.number().positive().optional(),
}, async ({ path: wav_path, spectrogram, timeline_seconds }) => analyze_wav(wav_path, { spectrogram, timeline_seconds }));

register("reaper_project_save", "Save the project (to a path, or in place).", {
    path: z.string().optional(),
}, async ({ path: project_path }) => bridge_request(call("project_save", project_path?.replaceAll("\\", "/"))));

register("reaper_project_clear", "Delete all tracks and markers in the current project. Requires confirm: true.", {
    confirm: z.literal(true),
}, async () => bridge_request(call("project_clear")));

const transport = new StdioServerTransport();
await server.connect(transport);
