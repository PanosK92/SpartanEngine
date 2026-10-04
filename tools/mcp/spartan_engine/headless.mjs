/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

import { spawn } from "node:child_process";
import fs from "node:fs/promises";
import net from "node:net";
import path from "node:path";
import { EngineClient } from "./engine_client.mjs";

const executable_names = process.platform === "win32"
  ? ["spartan_vulkan_development.exe", "spartan_vulkan.exe", "spartan_vulkan_debug.exe", "spartan_d3d12_development.exe", "spartan_d3d12.exe", "spartan_d3d12_debug.exe"]
  : ["spartan_vulkan_development", "spartan_vulkan", "spartan_vulkan_debug"];

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// the freshest build wins, so an agent that just rebuilt tests what it built
export async function find_engine_executable(binaries_directory, requested)
{
  if (requested)
  {
    const candidate = path.isAbsolute(requested) ? requested : path.join(binaries_directory, requested);
    await fs.access(candidate);
    return candidate;
  }

  let newest = null;
  for (const name of executable_names)
  {
    const candidate = path.join(binaries_directory, name);
    try
    {
      const stat = await fs.stat(candidate);
      if (!newest || stat.mtimeMs > newest.mtime)
      {
        newest = { path: candidate, mtime: stat.mtimeMs };
      }
    }
    catch
    {
    }
  }

  if (!newest)
  {
    throw new Error(`no spartan executable in ${binaries_directory}, build the engine first`);
  }
  return newest.path;
}

function port_is_free(host, port)
{
  return new Promise((resolve) =>
  {
    const probe = net.createServer();
    probe.once("error", () => resolve(false));
    probe.once("listening", () => probe.close(() => resolve(true)));
    probe.listen(port, host);
  });
}

export async function find_free_port(host, first, count = 100)
{
  for (let port = first; port < first + count; port++)
  {
    if (await port_is_free(host, port))
    {
      return port;
    }
  }
  throw new Error(`no free port in ${first}-${first + count - 1}`);
}

export function spawn_engine(executable, binaries_directory, args)
{
  const child = spawn(executable, args, {
    cwd: binaries_directory,
    stdio: "ignore",
    windowsHide: true,
  });
  child.exit_code = null;
  child.exited = new Promise((resolve) =>
  {
    child.once("exit", (code) =>
    {
      child.exit_code = code;
      resolve(code);
    });
    child.once("error", (error) =>
    {
      child.spawn_error = error.message;
      resolve(null);
    });
  });
  return child;
}

export async function wait_for_exit(child, timeout_ms)
{
  if (child.exitCode !== null || child.spawn_error)
  {
    return true;
  }
  return Promise.race([child.exited.then(() => true), sleep(timeout_ms).then(() => false)]);
}

export async function wait_for_bridge(host, port, child, timeout_ms)
{
  const probe = new EngineClient({ host, port, timeout_ms: 2000, connect_timeout_ms: 1000, source: "headless_probe", idle_close_ms: 0 });
  const deadline = Date.now() + timeout_ms;
  try
  {
    while (Date.now() < deadline)
    {
      if (child.exitCode !== null || child.spawn_error)
      {
        return { ok: false, error: child.spawn_error ?? `engine exited with code ${child.exitCode} before its bridge answered, see binaries/log_headless.txt` };
      }

      const result = await probe.command("engine_status", {}, 2000);
      if (result.ok)
      {
        return result;
      }
      probe.close();
      await sleep(500);
    }
    return { ok: false, error: `bridge on port ${port} did not answer within ${Math.round(timeout_ms / 1000)} s` };
  }
  finally
  {
    probe.close();
  }
}

export async function read_log_lines(binaries_directory, pattern, limit = 60)
{
  try
  {
    const text = await fs.readFile(path.join(binaries_directory, "log_headless.txt"), "utf8");
    return text.split(/\r?\n/).filter((line) => pattern.test(line)).slice(-limit);
  }
  catch
  {
    return [];
  }
}
