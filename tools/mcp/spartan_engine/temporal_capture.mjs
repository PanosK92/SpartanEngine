/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
*/

import fs from "node:fs/promises";
import path from "node:path";
import { z } from "zod";

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const limitation = "RGB deltas are not isolated noise or ghosting measurements. Translation alignment cannot remove parallax, disocclusions, reflections, exposure changes or animated objects. Compare identical camera takes/settings; use a stationary shot to inspect temporal noise.";

// Average pixels rather than point-sampling: keep subpixel detail from aliasing
// into the motion estimate. Bound both memory and the alignment search cost.
export function temporal_thumbnail(image, max_dimension = 256)
{
  const scale = Math.min(1, max_dimension / Math.max(image.width, image.height));
  const width = Math.max(1, Math.round(image.width * scale));
  const height = Math.max(1, Math.round(image.height * scale));
  const pixels = new Float32Array(width * height * 3);
  for (let y = 0; y < height; y++)
  for (let x = 0; x < width; x++)
  {
    const x0 = Math.floor(x * image.width / width), x1 = Math.floor((x + 1) * image.width / width);
    const y0 = Math.floor(y * image.height / height), y1 = Math.floor((y + 1) * image.height / height);
    for (let sy = y0; sy < y1; sy++)
    for (let sx = x0; sx < x1; sx++)
    for (let c = 0; c < 3; c++)
      pixels[(y * width + x) * 3 + c] += image.pixels[(sy * image.width + sx) * 4 + c] / ((x1 - x0) * (y1 - y0));
  }
  return { width, height, pixels };
}

export function temporal_pair(a, b, { max_shift = 8, threshold = 8 } = {})
{
  if (a.width !== b.width || a.height !== b.height)
    throw new Error("frame dimensions differ");
  const { width, height } = a;
  const radius = Math.max(0, Math.min(max_shift, Math.floor((Math.min(width, height) - 1) / 4)));
  // The search uses the same interior pixels for every displacement, so a
  // candidate cannot win merely by discarding a difficult image border.
  function error(dx, dy, stride)
  {
    let sum = 0, count = 0;
    for (let y = radius; y < height - radius; y += stride)
    for (let x = radius; x < width - radius; x += stride)
    for (let c = 0; c < 3; c++)
    {
      sum += Math.abs(b.pixels[(y * width + x) * 3 + c] - a.pixels[((y + dy) * width + x + dx) * 3 + c]);
      count++;
    }
    return sum / count;
  }
  let dx = 0, dy = 0, best = error(0, 0, 3);
  for (let sy = -radius; sy <= radius; sy++)
  for (let sx = -radius; sx <= radius; sx++)
  {
    const candidate = error(sx, sy, 3);
    if (candidate < best)
    {
      best = candidate; dx = sx; dy = sy;
    }
  }
  const raw_mae = error(0, 0, 1);
  let aligned_mae = error(dx, dy, 1);
  // Sparse search must not report worse alignment than the identity mapping.
  if (aligned_mae > raw_mae) { dx = 0; dy = 0; aligned_mae = raw_mae; }
  let squared = 0, changed = 0, count = 0;
  const heatmap = Buffer.alloc(width * height * 3);
  for (let y = radius; y < height - radius; y++)
  for (let x = radius; x < width - radius; x++)
  {
    let maximum = 0;
    for (let c = 0; c < 3; c++)
    {
      const delta = Math.abs(b.pixels[(y * width + x) * 3 + c] - a.pixels[((y + dy) * width + x + dx) * 3 + c]);
      squared += delta * delta;
      maximum = Math.max(maximum, delta);
    }
    changed += maximum > threshold ? 1 : 0;
    count++;
    heatmap[(y * width + x) * 3] = Math.min(255, Math.round(maximum * 4));
  }
  return {
    raw_mae, aligned_mae, aligned_rmse: Math.sqrt(squared / (count * 3)),
    changed_ratio: changed / count, shift: [dx, dy],
    alignment_improvement: raw_mae > 0 ? 1 - aligned_mae / raw_mae : 0,
    search_boundary: radius > 0 && (Math.abs(dx) === radius || Math.abs(dy) === radius),
    heatmap,
  };
}

export async function analyze_temporal_directory(directory, options, codecs)
{
  const names = (await fs.readdir(directory)).filter((name) => /^frame_\d{5}\.png$/.test(name)).sort();
  if (names.length < 2 || names.length > 240)
    throw new Error("analysis requires 2 to 240 frame_NNNNN.png files");
  if (names.some((name, i) => name !== `frame_${String(i).padStart(5, "0")}.png`))
    throw new Error("frame sequence has gaps or does not start at frame_00000.png");
  let previous, dimensions, worst = null;
  const pairs = [], previews = [], labels = [];
  const preview_indices = new Set([0, Math.floor((names.length - 1) / 3), Math.floor(2 * (names.length - 1) / 3), names.length - 1]);
  for (let i = 0; i < names.length; i++)
  {
    const decoded = codecs.png_decode(await fs.readFile(path.join(directory, names[i])));
    const size = [decoded.width, decoded.height];
    if (dimensions && (size[0] !== dimensions[0] || size[1] !== dimensions[1]))
      throw new Error(`frame dimensions changed at ${names[i]}`);
    dimensions = size;
    const current = temporal_thumbnail(decoded);
    if (preview_indices.has(i))
    {
      const pixels = Buffer.alloc(current.width * current.height * 4, 255);
      for (let p = 0; p < current.width * current.height; p++)
      for (let c = 0; c < 3; c++) pixels[p * 4 + c] = Math.round(current.pixels[p * 3 + c]);
      previews.push({ width: current.width, height: current.height, pixels });
      labels.push(`FRAME ${i}`);
    }
    if (previous)
    {
      const { heatmap, ...metrics } = temporal_pair(previous, current, options);
      pairs.push({ from: i - 1, to: i, ...metrics });
      if (!worst || metrics.aligned_mae > worst.mae)
        worst = { from: i - 1, to: i, mae: metrics.aligned_mae, heatmap, width: current.width, height: current.height };
    }
    previous = current;
  }
  const mean = (key) => pairs.reduce((sum, pair) => sum + pair[key], 0) / pairs.length;
  const report = {
    frames: names.length, dimensions, analysis_dimensions: [previous.width, previous.height],
    units: "0-255 RGB; shifts in analysis pixels map current pixels into the previous frame",
    threshold: options.threshold ?? 8, max_shift: options.max_shift ?? 8, limitation,
    summary: { raw_mae: mean("raw_mae"), aligned_mae: mean("aligned_mae"), aligned_rmse: mean("aligned_rmse"),
      changed_ratio: mean("changed_ratio"), worst_pair: [worst.from, worst.to], search_boundary_pairs: pairs.filter((p) => p.search_boundary).length },
    pairs,
  };
  return { report, sheet: codecs.contact_sheet(previews, labels, { columns: 2, tile_width: 384 }).png,
    heatmap: codecs.png_encode_rgb(worst.width, worst.height, worst.heatmap) };
}

export function register_temporal_capture(context, codecs)
{
  const { register_local_tool, send_engine_command, tool_result, structured_error, edit_tool, read_only, read_only_mode, binaries_directory } = context;
  let capturing = false;
  const analysis_schema = {
    max_shift: z.number().int().min(0).max(16).optional().describe("translation search radius in the 256-pixel analysis image; 0 for stationary-camera noise tests"),
    threshold: z.number().min(0).max(255).optional(),
  };
  async function analyze(directory, options)
  {
    const result = await analyze_temporal_directory(directory, options, codecs);
    const report_path = path.join(directory, "temporal_report.json");
    if (!read_only_mode)
    {
      await fs.writeFile(report_path, JSON.stringify(result.report, null, 2) + "\n");
      await fs.writeFile(path.join(directory, "contact_sheet.png"), result.sheet);
      await fs.writeFile(path.join(directory, "worst_residual.png"), result.heatmap);
    }
    const response = tool_result({ ok: true, directory, report_path: read_only_mode ? null : report_path,
      ...result.report, pairs: undefined });
    response.content.push({ type: "image", mimeType: "image/png", data: result.sheet.toString("base64") },
      { type: "image", mimeType: "image/png", data: result.heatmap.toString("base64") });
    return response;
  }
  register_local_tool("screenshot_sequence_analyze", {
    title: "analyze screenshot sequence",
    description: "Measure consecutive frame deltas and translation-aligned residuals in a captured frame_NNNNN.png sequence. Returns contact sheet, worst residual heatmap and JSON report with per-pair metrics. These are RGB differences, not a definitive ghosting/noise score. Stationary takes with max_shift 0 isolate temporal instability better. No engine connection required.",
    inputSchema: { directory: z.string().min(1), ...analysis_schema }, annotations: read_only,
  }, async (args) => {
    try { return await analyze(path.resolve(binaries_directory, args.directory), args); }
    catch (error) { return tool_result(structured_error(error.message, { code: "temporal_analysis_failed" })); }
  });
  register_local_tool("screenshot_sequence", {
    title: "capture consecutive screenshots",
    description: "Capture 2-240 consecutive rendered frames of the configured sequencer camera move, then analyze temporal residuals. Configure motion with sequencer_event_add (position_start/end, look_start/end, ease linear); a fixed shot tests noise. Uses native fixed-timestep rendering with stride 1, never an MCP camera/screenshot loop. Requires paused sequencer and a loaded world. Replays from time 0 with preroll, restores camera and timeline time/preview afterwards; simulated world state is not rewound. Each run creates a unique folder under binaries/temporal_captures. For long runs use async_task_start. Reports missing/dropped frames as errors.",
    inputSchema: {
      frames: z.number().int().min(2).max(240).default(32), fps: z.number().min(1).max(120).default(60),
      start: z.number().min(0).default(0), preroll: z.number().min(0).max(5).default(1),
      timeout_s: z.number().min(5).max(600).default(180), ...analysis_schema,
    }, annotations: edit_tool,
  }, async (args) => {
    if (capturing) return tool_result(structured_error("a temporal capture is already running", { code: "capture_busy" }));
    capturing = true;
    let state, camera, started = false, directory, response;
    async function command(name, params = {})
    {
      const result = await send_engine_command(name, params);
      if (!result.ok) throw new Error(result.error || `${name} failed`);
      return result;
    }
    try
    {
      const status = await command("engine_status");
      if (status.loading || status.minimized) throw new Error("world must be loaded and rendering");
      state = await command("sequencer_get");
      if (state.playing || state.render?.active) throw new Error("pause the sequencer and finish any current render first");
      const end = args.start + (args.frames - 1) / args.fps;
      if (end > state.duration) throw new Error("requested frames extend past the sequence duration");
      camera = await command("camera_snapshot");
      const root = path.join(binaries_directory, "temporal_captures");
      await fs.mkdir(root, { recursive: true });
      directory = await fs.mkdtemp(path.join(root, "take-"));
      const world = await command("world_summary");
      const settings = {};
      for (const name of ["r.restir_pt", "r.restir_pt_scale", "r.restir_pt_reference", "r.restir_pt_async"])
        settings[name] = await command("cvar_get", { name });
      await fs.writeFile(path.join(directory, "capture.json"), JSON.stringify({ ...args, world, camera, settings, sequence: state }, null, 2) + "\n");
      const result = await command("sequencer_render", { action: "start", fps: args.fps, start: args.start, end, preroll: args.preroll, stride: 1, directory });
      started = true;
      let render = result.render;
      const deadline = Date.now() + args.timeout_s * 1000;
      while (render?.active && Date.now() < deadline)
      {
        await sleep(500);
        // A read can race the bridge's idle connection close. Retrying status
        // is safe; never retry start/stop mutations that may have executed.
        let status = await send_engine_command("sequencer_get");
        if (!status.ok)
        {
          await sleep(100);
          status = await command("sequencer_get");
        }
        render = status.render;
      }
      if (!render || render.active) throw new Error("temporal capture timed out");
      if (render.last_error || render.frames_written !== args.frames || render.frames_total !== args.frames)
        throw new Error(render.last_error || `expected ${args.frames} frames, got ${render.frames_written}`);
      response = await analyze(directory, args);
    }
    catch (error)
    {
      response = tool_result(structured_error(error.message, { code: "temporal_capture_failed", directory }));
    }
    finally
    {
      if (started)
      {
        try
        {
          await command("sequencer_render", { action: "stop" });
          await command("sequencer_set", { time: state.time, preview: state.preview });
          await command("camera_set_view", { position: camera.position, target: camera.position.map((v, i) => v + camera.forward[i]) });
        }
        catch (error)
        {
          response = tool_result(structured_error(`capture cleanup failed: ${error.message}`, { code: "capture_cleanup_failed", directory }));
        }
      }
      capturing = false;
    }
    return response;
  });
}
