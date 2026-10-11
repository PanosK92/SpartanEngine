/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

import { spawn } from "node:child_process";
import fs from "node:fs/promises";
import path from "node:path";
import zlib from "node:zlib";
import { z } from "zod";

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// png

function paeth(a, b, c)
{
  const p  = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc)
  {
    return a;
  }
  return pb <= pc ? b : c;
}

// decodes non-interlaced 8/16 bit grey, grey+alpha, rgb, rgba and palette pngs into rgba8
export function png_decode(buffer)
{
  const signature = [137, 80, 78, 71, 13, 10, 26, 10];
  if (buffer.length < 8 || signature.some((value, index) => buffer[index] !== value))
  {
    throw new Error("not a png file");
  }

  let width = 0, height = 0, bit_depth = 0, color_type = 0, interlace = 0;
  let palette = null, palette_alpha = null;
  const idat = [];
  let offset = 8;
  while (offset + 8 <= buffer.length)
  {
    const length = buffer.readUInt32BE(offset);
    const type   = buffer.toString("ascii", offset + 4, offset + 8);
    const data   = buffer.subarray(offset + 8, offset + 8 + length);
    offset += 12 + length;
    if (type === "IHDR")
    {
      width      = data.readUInt32BE(0);
      height     = data.readUInt32BE(4);
      bit_depth  = data[8];
      color_type = data[9];
      interlace  = data[12];
    }
    else if (type === "PLTE")
    {
      palette = data;
    }
    else if (type === "tRNS")
    {
      palette_alpha = data;
    }
    else if (type === "IDAT")
    {
      idat.push(data);
    }
    else if (type === "IEND")
    {
      break;
    }
  }

  const channels_by_type = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };
  const channels = channels_by_type[color_type];
  if (!channels || interlace !== 0 || (bit_depth !== 8 && bit_depth !== 16) || (color_type === 3 && bit_depth !== 8))
  {
    throw new Error(`unsupported png format (color type ${color_type}, bit depth ${bit_depth}, interlace ${interlace})`);
  }

  const raw          = zlib.inflateSync(Buffer.concat(idat));
  const bpp          = channels * (bit_depth / 8);
  const stride       = width * bpp;
  const pixels       = Buffer.alloc(width * height * 4);
  let previous       = Buffer.alloc(stride);
  let current        = Buffer.alloc(stride);
  for (let y = 0; y < height; y++)
  {
    const row_start = y * (stride + 1);
    const filter    = raw[row_start];
    for (let x = 0; x < stride; x++)
    {
      const value = raw[row_start + 1 + x];
      const left  = x >= bpp ? current[x - bpp] : 0;
      const up    = previous[x];
      const diag  = x >= bpp ? previous[x - bpp] : 0;
      let decoded = value;
      if (filter === 1)
      {
        decoded = value + left;
      }
      else if (filter === 2)
      {
        decoded = value + up;
      }
      else if (filter === 3)
      {
        decoded = value + ((left + up) >> 1);
      }
      else if (filter === 4)
      {
        decoded = value + paeth(left, up, diag);
      }
      current[x] = decoded & 255;
    }

    const step = bit_depth / 8;
    for (let x = 0; x < width; x++)
    {
      const source = x * bpp;
      const sample = (channel) => current[source + channel * step];
      const target = (y * width + x) * 4;
      if (color_type === 3)
      {
        const index = sample(0);
        pixels[target]     = palette ? palette[index * 3] : index;
        pixels[target + 1] = palette ? palette[index * 3 + 1] : index;
        pixels[target + 2] = palette ? palette[index * 3 + 2] : index;
        pixels[target + 3] = palette_alpha && index < palette_alpha.length ? palette_alpha[index] : 255;
      }
      else if (channels <= 2)
      {
        pixels[target] = pixels[target + 1] = pixels[target + 2] = sample(0);
        pixels[target + 3] = channels === 2 ? sample(1) : 255;
      }
      else
      {
        pixels[target]     = sample(0);
        pixels[target + 1] = sample(1);
        pixels[target + 2] = sample(2);
        pixels[target + 3] = channels === 4 ? sample(3) : 255;
      }
    }
    [previous, current] = [current, previous];
  }

  return { width, height, pixels };
}

function png_chunk(type, data)
{
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc  = Buffer.alloc(4);
  crc.writeUInt32BE(zlib.crc32(body) >>> 0);
  return Buffer.concat([length, body, crc]);
}

export function png_encode_rgb(width, height, rgb)
{
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  const stride = width * 3;
  const raw    = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++)
  {
    rgb.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    png_chunk("IHDR", header),
    png_chunk("IDAT", zlib.deflateSync(raw, { level: 6 })),
    png_chunk("IEND", Buffer.alloc(0)),
  ]);
}

// changed pixels are red over a dimmed grey copy of b, the image is box-downscaled so it stays readable inline
export function compare_images(a, b, threshold, max_width = 1024)
{
  if (a.width !== b.width || a.height !== b.height)
  {
    throw new Error(`image sizes differ: ${a.width}x${a.height} vs ${b.width}x${b.height}`);
  }

  const { width, height } = a;
  const scale      = Math.max(1, Math.ceil(width / max_width));
  const out_width  = Math.floor(width / scale);
  const out_height = Math.floor(height / scale);
  const diff_sum   = new Float64Array(out_width * out_height * 3);
  const diff_count = new Uint32Array(out_width * out_height);
  let abs_sum = 0, square_sum = 0, changed = 0;
  let min_x = width, min_y = height, max_x = -1, max_y = -1;
  for (let y = 0; y < height; y++)
  {
    for (let x = 0; x < width; x++)
    {
      const index = (y * width + x) * 4;
      const dr = Math.abs(a.pixels[index] - b.pixels[index]);
      const dg = Math.abs(a.pixels[index + 1] - b.pixels[index + 1]);
      const db = Math.abs(a.pixels[index + 2] - b.pixels[index + 2]);
      abs_sum    += dr + dg + db;
      square_sum += dr * dr + dg * dg + db * db;
      const is_changed = Math.max(dr, dg, db) > threshold;
      if (is_changed)
      {
        changed++;
        min_x = Math.min(min_x, x);
        min_y = Math.min(min_y, y);
        max_x = Math.max(max_x, x);
        max_y = Math.max(max_y, y);
      }

      const ox = Math.floor(x / scale);
      const oy = Math.floor(y / scale);
      if (ox >= out_width || oy >= out_height)
      {
        continue;
      }
      const out_index = oy * out_width + ox;
      const grey      = (b.pixels[index] * 0.3 + b.pixels[index + 1] * 0.59 + b.pixels[index + 2] * 0.11) * 0.35;
      diff_sum[out_index * 3]     += is_changed ? 255 : grey;
      diff_sum[out_index * 3 + 1] += is_changed ? 40 : grey;
      diff_sum[out_index * 3 + 2] += is_changed ? 40 : grey;
      diff_count[out_index]++;
    }
  }

  const diff_rgb = Buffer.alloc(out_width * out_height * 3);
  for (let i = 0; i < out_width * out_height; i++)
  {
    const count = Math.max(1, diff_count[i]);
    diff_rgb[i * 3]     = Math.round(diff_sum[i * 3] / count);
    diff_rgb[i * 3 + 1] = Math.round(diff_sum[i * 3 + 1] / count);
    diff_rgb[i * 3 + 2] = Math.round(diff_sum[i * 3 + 2] / count);
  }

  const samples = width * height * 3;
  return {
    width,
    height,
    threshold,
    identical: abs_sum === 0,
    mean_abs_diff: Number((abs_sum / samples).toFixed(4)),
    rmse: Number(Math.sqrt(square_sum / samples).toFixed(4)),
    changed_pixels: changed,
    changed_ratio: Number((changed / (width * height)).toFixed(6)),
    changed_bounds: max_x >= 0 ? [min_x, min_y, max_x, max_y] : null,
    diff_png: png_encode_rgb(out_width, out_height, diff_rgb),
    diff_scale: scale,
  };
}

// batch references

const reference_pattern = /^\$([A-Za-z_]\w*|\d+)((?:\.[\w-]+)*)$/;

function resolve_references(value, outputs)
{
  if (typeof value === "string")
  {
    const match = value.match(reference_pattern);
    if (!match || !outputs.has(match[1]))
    {
      return value;
    }
    let resolved = outputs.get(match[1]);
    for (const key of match[2].split(".").filter(Boolean))
    {
      const next = resolved !== null && typeof resolved === "object" ? resolved[key] : undefined;
      if (next === undefined)
      {
        const available = resolved !== null && typeof resolved === "object" ? Object.keys(resolved).slice(0, 24).join(", ") : typeof resolved;
        throw new Error(`reference ${value} does not resolve, '${key}' not found (available: ${available})`);
      }
      resolved = next;
    }
    return resolved;
  }
  if (Array.isArray(value))
  {
    return value.map((entry) => resolve_references(entry, outputs));
  }
  if (value !== null && typeof value === "object")
  {
    return Object.fromEntries(Object.entries(value).map(([key, entry]) => [key, resolve_references(entry, outputs)]));
  }
  return value;
}

// registration

export function register_agent_toolkit(context)
{
  const {
    register_local_tool,
    tool_registry,
    send_engine_command,
    tool_result,
    structured_error,
    normalize_result,
    read_only,
    edit_tool,
    read_only_mode,
    project_root,
    binaries_directory,
    engine,
    engine_host,
    find_engine_executable,
    wait_for_bridge,
    headless_running,
  } = context;

  const vector3          = z.array(z.number()).length(3);
  const viewpoints_path  = path.join(project_root, "binaries", "project", "mcp", "viewpoints.json");
  const thumbnails_dir   = path.join(project_root, "binaries", "project", "mcp", "blockout", "thumbnails");
  let launched_engine    = null;

  const slug = (text) => String(text).toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 48) || "x";

  const normalize_world = (file_path) => String(file_path ?? "").replaceAll("\\", "/").toLowerCase();

  function images_of(result)
  {
    return (result?.content ?? []).filter((entry) => entry.type === "image");
  }

  function with_images(structured, images)
  {
    const normalized = normalize_result(structured);
    return {
      structuredContent: normalized,
      content: [{ type: "text", text: JSON.stringify(normalized) }, ...images],
      isError: !normalized.ok,
    };
  }

  async function minimized_error()
  {
    const status = await send_engine_command("engine_status");
    if (status.ok && status.minimized)
    {
      return structured_error("the engine window is minimized, it stops rendering so screenshots never complete", {
        code: "window_minimized",
        retryable: true,
        suggested_action: "call window_restore (the user may have minimized it on purpose, ask if unsure) or use headless_start for a private instance",
      });
    }
    return null;
  }

  async function current_world_key()
  {
    const world = await send_engine_command("world_summary");
    const key = world.ok ? normalize_world(world.file_path) : "";
    return key || "unsaved";
  }

  async function read_viewpoints()
  {
    try
    {
      return JSON.parse(await fs.readFile(viewpoints_path, "utf8"));
    }
    catch
    {
      return { worlds: {} };
    }
  }

  async function write_viewpoints(data)
  {
    await fs.mkdir(path.dirname(viewpoints_path), { recursive: true });
    await fs.writeFile(viewpoints_path, `${JSON.stringify(data, null, 2)}\n`, "utf8");
  }

  function resolve_image_path(file)
  {
    if (path.isAbsolute(file))
    {
      return file;
    }
    const name = path.basename(file);
    return path.join(thumbnails_dir, name.toLowerCase().endsWith(".png") ? name : `${name}.png`);
  }

  async function compare_files(path_a, path_b, threshold, diff_path)
  {
    const a = png_decode(await fs.readFile(path_a));
    const b = png_decode(await fs.readFile(path_b));
    const comparison = compare_images(a, b, threshold);
    const { diff_png, ...metrics } = comparison;
    if (diff_path)
    {
      await fs.mkdir(path.dirname(diff_path), { recursive: true });
      await fs.writeFile(diff_path, diff_png);
    }
    return { metrics: { a: path_a, b: path_b, diff_path: diff_path ?? null, ...metrics }, diff_png };
  }

  async function wait_until(until, { world, frames = 10, timeout_ms, poll_ms = 500 })
  {
    const wanted     = world ? normalize_world(world) : null;
    const started    = Date.now();
    const deadline   = started + timeout_ms;
    let first_frame  = null;
    let last_status  = null;
    let last_world   = null;
    while (Date.now() < deadline)
    {
      last_status = await send_engine_command("engine_status");
      if (last_status.ok)
      {
        first_frame ??= last_status.frame_number;
        let done = !last_status.loading;
        if (done && until === "jobs_idle")
        {
          done = !last_status.jobs_running;
        }
        if (done && until === "frames")
        {
          done = last_status.frame_number - first_frame >= frames;
        }
        if (done && until === "world")
        {
          last_world = await send_engine_command("world_summary");
          done = last_world.ok && normalize_world(last_world.file_path) === wanted;
        }
        if (done)
        {
          return { ok: true, until, waited_ms: Date.now() - started, status: last_status, world: last_world ?? undefined };
        }
      }
      await sleep(poll_ms);
    }

    const stalled = until === "frames" && last_status?.ok && last_status.frame_number === first_frame;
    return structured_error(`engine did not reach '${until}' within ${Math.round(timeout_ms / 1000)} s`, {
      code: "wait_timeout",
      retryable: true,
      status: last_status,
      suggested_action: last_status?.minimized || stalled
        ? "frames are not advancing, the window is probably minimized: call window_restore"
        : (last_status?.ok ? "increase timeout_s or check console_read" : "the bridge is not answering, use engine_ensure"),
    });
  }

  register_local_tool("tool_batch", {
    title: "tool batch",
    description: [
      "Run several Spartan MCP tools in order in ONE call and get every result back, plus all screenshot images.",
      "Saves round trips for multi-step edits such as resolve, transform, frame, screenshot.",
      "A string argument written as $<label>.<path> or $<step index>.<path> is replaced by a value from an earlier step's result,",
      "e.g. steps [{label:'car', tool:'entity_resolve', args:{name:'car'}}, {tool:'viewport_frame', args:{id:'$car.id'}}].",
      "Array indices work too ($0.entities.0.id). Stops at the first failure unless stop_on_error is false or the step sets continue_on_error.",
      "summary_only drops step results except ok/error to keep the reply small.",
    ].join(" "),
    inputSchema: {
      steps: z.array(z.object({
        tool: z.string().min(1),
        args: z.record(z.string(), z.any()).optional(),
        label: z.string().regex(/^[A-Za-z_]\w*$/).optional(),
        continue_on_error: z.boolean().optional(),
      })).min(1).max(64),
      stop_on_error: z.boolean().optional(),
      summary_only: z.boolean().optional(),
      max_images: z.number().int().min(0).max(16).optional(),
    },
    annotations: read_only_mode ? read_only : edit_tool,
    traceLocal: false,
  }, async ({ steps, stop_on_error = true, summary_only = false, max_images = 8 }) =>
  {
    const outputs  = new Map();
    const receipts = [];
    const images   = [];
    let failed_step = null;
    for (const [index, step] of steps.entries())
    {
      const started = Date.now();
      const label   = step.label ?? null;
      const tool    = tool_registry.get(step.tool);
      let result;
      if (step.tool === "tool_batch")
      {
        result = structured_error("tool_batch cannot nest itself", { code: "invalid_arguments" });
      }
      else if (!tool)
      {
        result = structured_error(`unknown tool ${step.tool}`, {
          code: "target_resolution_failed",
          suggested_action: read_only_mode ? "the server is read-only, mutating tools are not registered" : "use search_capabilities",
        });
      }
      else
      {
        try
        {
          const args     = resolve_references(step.args ?? {}, outputs);
          const response = await tool.handler(args);
          result = response?.structuredContent ?? { ok: !response?.isError };
          for (const image of images_of(response))
          {
            if (images.length < max_images)
            {
              images.push(image);
            }
          }
        }
        catch (error)
        {
          result = structured_error(error.message, { code: "batch_step_failed" });
        }
      }

      const ok = result?.ok !== false;
      outputs.set(String(index), result);
      if (label)
      {
        outputs.set(label, result);
      }
      receipts.push({
        index,
        label,
        tool: step.tool,
        ok,
        duration_ms: Date.now() - started,
        ...(summary_only ? { error: result?.error } : { result }),
      });
      if (!ok && failed_step === null)
      {
        failed_step = index;
      }
      if (!ok && stop_on_error && !step.continue_on_error)
      {
        break;
      }
    }

    return with_images({
      ok: failed_step === null,
      error: failed_step === null ? undefined : `step ${failed_step} (${steps[failed_step].tool}) failed`,
      completed: receipts.length,
      total: steps.length,
      failed_step,
      steps: receipts,
    }, images);
  });

  register_local_tool("engine_wait", {
    title: "engine wait",
    description: "Block until the engine reaches a state instead of polling by hand. until: 'ready' (bridge answers and no world is loading, default), 'world' (the given absolute world path finished loading), 'jobs_idle' (also no background jobs), 'frames' (at least frames more frames rendered, which proves rendering is not stalled by a minimized window). Returns the final engine_status.",
    inputSchema: {
      until: z.enum(["ready", "world", "jobs_idle", "frames"]).optional(),
      world: z.string().optional(),
      frames: z.number().int().min(1).max(10000).optional(),
      timeout_s: z.number().min(1).max(900).optional(),
    },
    annotations: read_only,
  }, async ({ until = "ready", world, frames, timeout_s = 120 }) =>
  {
    if (until === "world" && !world)
    {
      return tool_result(structured_error("until 'world' needs an absolute world path", { code: "invalid_arguments" }));
    }
    return tool_result(await wait_until(until, { world, frames, timeout_ms: timeout_s * 1000 }));
  });

  register_local_tool("engine_ensure", {
    title: "engine ensure",
    description: [
      "One-call session setup: make sure a Spartan engine answers on the port this server targets, launching the newest visible editor build from binaries/ (--mcp-control --no-steam) only if nothing answers,",
      "then optionally load an absolute world path and wait until it finished loading, and optionally switch mode (edit, play, pause, resume).",
      "It never closes an instance, never reloads a world that is already loaded, and refuses to replace a different loaded world unless it launched the engine in this same call",
      "(a second world_load per session blows up GPU memory) or force_load is true. Returns status, world summary and whether it launched.",
    ].join(" "),
    inputSchema: {
      world: z.string().optional(),
      mode: z.enum(["edit", "play", "pause", "resume"]).optional(),
      launch: z.boolean().optional(),
      executable: z.string().optional(),
      force_load: z.boolean().optional(),
      timeout_s: z.number().min(10).max(900).optional(),
    },
    annotations: edit_tool,
  }, async ({ world, mode, launch = true, executable, force_load = false, timeout_s = 300 }) =>
  {
    const timeout_ms = timeout_s * 1000;
    if (world && (!path.isAbsolute(world) || !world.toLowerCase().endsWith(".world")))
    {
      return tool_result(structured_error("world must be an absolute .world path", { code: "invalid_arguments" }));
    }

    let launched = false;
    let status = await send_engine_command("engine_status");
    if (!status.ok)
    {
      if (headless_running())
      {
        return tool_result(structured_error("the headless instance this server started is not answering", {
          code: "engine_unavailable",
          suggested_action: "headless_stop, then retry",
        }));
      }
      if (!launch)
      {
        return tool_result(structured_error(`no engine answers on port ${engine.port}`, {
          code: "engine_unavailable",
          suggested_action: "call engine_ensure with launch true",
        }));
      }

      let resolved;
      try
      {
        resolved = await find_engine_executable(binaries_directory, executable);
      }
      catch (error)
      {
        return tool_result(structured_error(error.message, { code: "engine_launch_failed" }));
      }

      // detached so the editor outlives this server when the mcp client restarts it
      const child = spawn(resolved, ["--mcp-control", `--mcp-port=${engine.port}`, "--no-steam"], {
        cwd: binaries_directory,
        stdio: "ignore",
        detached: true,
        windowsHide: false,
      });
      child.exited = new Promise((resolve) =>
      {
        child.once("exit", resolve);
        child.once("error", (error) =>
        {
          child.spawn_error = error.message;
          resolve(null);
        });
      });
      child.unref();
      launched_engine = { pid: child.pid, port: engine.port, executable: resolved, started_at: new Date().toISOString() };
      engine.close();
      status = await wait_for_bridge(engine_host, engine.port, child, timeout_ms);
      if (!status.ok)
      {
        return tool_result({ ...status, launched: true, executable: resolved });
      }
      launched = true;
    }

    let world_summary = await send_engine_command("world_summary");
    let load = null;
    if (world)
    {
      const loaded_path = world_summary.ok ? normalize_world(world_summary.file_path) : "";
      if (loaded_path === normalize_world(world))
      {
        load = { skipped: true, reason: "already loaded" };
      }
      else if (loaded_path && !launched && !force_load)
      {
        return tool_result(structured_error(`another world is already loaded in this engine session (${world_summary.file_path})`, {
          code: "world_load_refused",
          retryable: false,
          suggested_action: "work in the loaded world, use headless_start {world} for a private instance, or pass force_load true if a second load in this session is acceptable",
          world: world_summary,
        }));
      }
      else
      {
        const waited = await wait_until("ready", { timeout_ms });
        const result = waited.ok ? await send_engine_command("world_load", { path: world }) : waited;
        if (!result.ok)
        {
          return tool_result({ ...result, launched });
        }
        const loaded = await wait_until("world", { world, timeout_ms });
        if (!loaded.ok)
        {
          return tool_result({ ...loaded, launched });
        }
        world_summary = loaded.world;
        load = { skipped: false, waited_ms: loaded.waited_ms };
      }
    }

    if (mode)
    {
      status = await send_engine_command("engine_set_mode", { mode });
      if (!status.ok)
      {
        return tool_result({ ...status, launched });
      }
    }
    else
    {
      status = await send_engine_command("engine_status");
    }

    return tool_result({
      ok: true,
      launched,
      launched_engine: launched_engine?.port === engine.port ? launched_engine : null,
      port: engine.port,
      status,
      world: world_summary,
      load,
      note: status.minimized ? "the engine window is minimized, call window_restore before screenshots" : undefined,
    });
  });

  register_local_tool("viewpoint_save", {
    title: "viewpoint save",
    description: "Save a named camera viewpoint for the loaded world so before/after screenshots are taken from exactly the same place, across sessions and agents (stored in binaries/project/mcp/viewpoints.json, keyed by world path). Pass position and target (world space), or omit both to store the live camera. Saving an existing name replaces it.",
    inputSchema: {
      name: z.string().min(1).max(64),
      position: vector3.optional(),
      target: vector3.optional(),
      note: z.string().max(200).optional(),
    },
    annotations: edit_tool,
  }, async ({ name, position, target, note }) =>
  {
    if (Boolean(position) !== Boolean(target))
    {
      return tool_result(structured_error("pass both position and target, or neither to use the live camera", { code: "invalid_arguments" }));
    }
    if (!position)
    {
      const camera = await send_engine_command("camera_snapshot");
      if (!camera.ok)
      {
        return tool_result(camera);
      }
      position = camera.position;
      target   = camera.position.map((value, index) => value + camera.forward[index] * 10);
    }
    position = position.map((value) => Number(value.toFixed(4)));
    target   = target.map((value) => Number(value.toFixed(4)));

    const world = await current_world_key();
    const data  = await read_viewpoints();
    data.worlds[world] ??= {};
    data.worlds[world][name] = { position, target, note, saved_at: new Date().toISOString() };
    await write_viewpoints(data);
    return tool_result({ ok: true, world, name, viewpoint: data.worlds[world][name], count: Object.keys(data.worlds[world]).length });
  });

  register_local_tool("viewpoint_list", {
    title: "viewpoint list",
    description: "List the named camera viewpoints saved for the loaded world (or every world with all_worlds).",
    inputSchema: {
      all_worlds: z.boolean().optional(),
    },
    annotations: read_only,
  }, async ({ all_worlds = false }) =>
  {
    const data = await read_viewpoints();
    if (all_worlds)
    {
      return tool_result({ ok: true, path: viewpoints_path, worlds: data.worlds });
    }
    const world = await current_world_key();
    return tool_result({ ok: true, path: viewpoints_path, world, viewpoints: data.worlds[world] ?? {} });
  });

  register_local_tool("viewpoint_delete", {
    title: "viewpoint delete",
    description: "Delete named camera viewpoints of the loaded world.",
    inputSchema: {
      names: z.array(z.string().min(1)).min(1),
    },
    annotations: edit_tool,
  }, async ({ names }) =>
  {
    const world   = await current_world_key();
    const data    = await read_viewpoints();
    const entries = data.worlds[world] ?? {};
    const deleted = names.filter((name) => name in entries);
    for (const name of deleted)
    {
      delete entries[name];
    }
    await write_viewpoints(data);
    return tool_result({ ok: true, world, deleted, missing: names.filter((name) => !deleted.includes(name)) });
  });

  register_local_tool("viewpoint_capture", {
    title: "viewpoint capture",
    description: [
      "Screenshot saved viewpoints (all of them, or names) one after another and return every image, then restore the user's camera.",
      "Each shot is written as vp_<world>_<viewpoint>_<label>.png, so capture with label 'before', edit, then capture with label 'after' and compare_to 'before'",
      "to get per-viewpoint pixel metrics (changed_ratio, mean_abs_diff, changed_bounds) and a red-on-grey diff image.",
      "settle_ms waits after moving the camera (raise it to ~10000 after big view jumps so auto exposure settles). ui true captures the editor window.",
    ].join(" "),
    inputSchema: {
      names: z.array(z.string().min(1)).optional(),
      label: z.string().min(1).max(32).optional(),
      compare_to: z.string().min(1).max(32).optional(),
      threshold: z.number().int().min(0).max(255).optional(),
      settle_ms: z.number().int().min(0).max(30000).optional(),
      wait_ms: z.number().int().min(0).max(10000).optional(),
      ui: z.boolean().optional(),
      restore_camera: z.boolean().optional(),
      include_diff_images: z.boolean().optional(),
    },
    annotations: edit_tool,
  }, async ({ names, label = "current", compare_to, threshold = 8, settle_ms = 800, wait_ms = 5000, ui, restore_camera = true, include_diff_images = true }) =>
  {
    const world     = await current_world_key();
    const saved     = (await read_viewpoints()).worlds[world] ?? {};
    const requested = names ?? Object.keys(saved);
    const missing   = requested.filter((name) => !(name in saved));
    if (requested.length === 0 || missing.length > 0)
    {
      return tool_result(structured_error(requested.length === 0 ? "no viewpoints saved for this world" : `unknown viewpoints: ${missing.join(", ")}`, {
        code: "target_resolution_failed",
        suggested_action: "viewpoint_list, or viewpoint_save first",
        available: Object.keys(saved),
      }));
    }
    const minimized = await minimized_error();
    if (minimized)
    {
      return tool_result(minimized);
    }

    const screenshot = tool_registry.get("screenshot_take");
    const original   = restore_camera ? await send_engine_command("camera_snapshot") : null;
    const shots      = [];
    const images     = [];
    try
    {
      for (const name of requested)
      {
        const viewpoint = saved[name];
        const camera = await send_engine_command("camera_set_view", { position: viewpoint.position, target: viewpoint.target });
        if (!camera.ok)
        {
          shots.push({ name, ok: false, error: camera.error });
          continue;
        }
        await sleep(settle_ms);
        const file     = `vp_${slug(path.basename(world, ".world"))}_${slug(name)}_${slug(label)}.png`;
        const response = await screenshot.handler({ path: file, wait_ms, ui });
        const shot     = response.structuredContent ?? { ok: false };
        const entry    = { name, ok: Boolean(shot.ok && shot.ready), path: shot.path, error: shot.error };
        images.push(...images_of(response));
        if (compare_to && shot.path && shot.ready)
        {
          const baseline = path.join(path.dirname(shot.path), file.replace(new RegExp(`_${slug(label)}\\.png$`), `_${slug(compare_to)}.png`));
          try
          {
            const diff_path = shot.path.replace(/\.png$/i, `_vs_${slug(compare_to)}_diff.png`);
            const { metrics, diff_png } = await compare_files(baseline, shot.path, threshold, diff_path);
            entry.comparison = metrics;
            if (include_diff_images && !metrics.identical)
            {
              images.push({ type: "image", mimeType: "image/png", data: diff_png.toString("base64") });
            }
          }
          catch (error)
          {
            entry.comparison = { ok: false, error: `${error.message} (baseline ${baseline})` };
          }
        }
        shots.push(entry);
      }
    }
    finally
    {
      if (original?.ok)
      {
        await send_engine_command("camera_set_view", {
          position: original.position,
          target: original.position.map((value, index) => value + original.forward[index]),
        });
      }
    }

    const failed = shots.filter((shot) => !shot.ok).map((shot) => shot.name);
    return with_images({
      ok: failed.length === 0,
      error: failed.length ? `capture failed for ${failed.join(", ")}` : undefined,
      world,
      label,
      compare_to,
      shots,
    }, images);
  });

  register_local_tool("screenshot_compare", {
    title: "screenshot compare",
    description: "Compare two PNG screenshots pixel by pixel to prove whether a change is visible (or that an optimization is pixel-identical). a is the baseline, b the new image; absolute paths or file names in the MCP thumbnails folder (binaries/project/mcp/blockout/thumbnails). Returns identical, mean_abs_diff and rmse (0-255), changed_ratio and changed_bounds [x0,y0,x1,y1] for pixels whose largest channel difference exceeds threshold, and a red-on-grey diff image (also written next to b).",
    inputSchema: {
      a: z.string().min(1),
      b: z.string().min(1),
      threshold: z.number().int().min(0).max(255).optional(),
      include_image: z.boolean().optional(),
    },
    annotations: read_only,
  }, async ({ a, b, threshold = 8, include_image = true }) =>
  {
    const path_a = resolve_image_path(a);
    const path_b = resolve_image_path(b);
    try
    {
      const diff_path = path_b.replace(/\.png$/i, "_diff.png");
      const { metrics, diff_png } = await compare_files(path_a, path_b, threshold, read_only_mode ? null : diff_path);
      const images = include_image ? [{ type: "image", mimeType: "image/png", data: diff_png.toString("base64") }] : [];
      return with_images({ ok: true, ...metrics }, images);
    }
    catch (error)
    {
      return tool_result(structured_error(error.message, { code: "screenshot_compare_failed", a: path_a, b: path_b }));
    }
  });

  return { minimized_error };
}
