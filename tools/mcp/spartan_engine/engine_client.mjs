import net from "node:net";
import { append_debug_log } from "./debug_log.mjs";

function protocol_value(value) {
  if (Array.isArray(value))
  {
    return value.join(",");
  }

  if (typeof value === "boolean")
  {
    return value ? "true" : "false";
  }

  return String(value);
}

function command_line(command, args = {}) {
  const parts = [command];
  for (const [key, value] of Object.entries(args))
  {
    if (value === undefined || value === null)
    {
      continue;
    }

    parts.push(`${key}=${encodeURIComponent(protocol_value(value))}`);
  }

  return `${parts.join(" ")}\n`;
}

export class EngineClient {
  constructor({ host, port, timeout_ms, source = "engine_client" }) {
    this.host = host;
    this.port = port;
    this.timeout_ms = timeout_ms;
    this.source = source;
    this.socket = null;
    this.buffer = "";
    this.pending = [];
    this.pending_by_request_id = new Map();
    this.next_request_id = 1;
    this.connecting = null;
    this.idle_close_timer = null;
  }

  async command(command, args = {}, timeout_ms = this.timeout_ms) {
    this.cancel_idle_close();
    const started_at = Date.now();
    const request_id = `${this.source}-${Date.now().toString(36)}-${this.next_request_id++}`;
    const finish = (result, phase = "response") => {
      void append_debug_log({
        type: "engine_command",
        source: this.source,
        request_id,
        host: this.host,
        port: this.port,
        command,
        args,
        timeout_ms,
        duration_ms: Date.now() - started_at,
        phase,
        ok: Boolean(result?.ok),
        result,
      });
      return result;
    };

    const connected = await this.ensure_connected(timeout_ms);
    if (!connected)
    {
      return finish({
        ok: false,
        error: `engine connection for ${command} timed out after ${timeout_ms}ms`,
        code: "engine_connect_timeout",
        retryable: true,
        suggested_action: "restart the engine MCP bridge or close the client currently holding the bridge connection",
      }, "connect");
    }
    if (!this.socket || this.socket.destroyed)
    {
      this.close();
      const reconnected = await this.ensure_connected(timeout_ms);
      if (!reconnected || !this.socket || this.socket.destroyed)
      {
        return finish({ ok: false, error: "engine connection is not available" }, "connect");
      }
    }

    return new Promise((resolve) => {
      const request = {
        request_id,
        resolve,
        finish,
        completed: false,
        timed_out: false,
        timer: setTimeout(() => {
          request.timed_out = true;
          this.finish_request(request, request.finish({
            ok: false,
            error: `engine command ${command} timed out after ${timeout_ms}ms`,
            code: "engine_timeout",
            command,
            request_id,
            retryable: true,
            suggested_action: "retry once, then use a smaller operation or a native batch command",
          }, "timeout"));
          this.close();
        }, timeout_ms),
      };

      this.pending.push(request);
      this.pending_by_request_id.set(request_id, request);
      try
      {
        this.socket.write(command_line(command, { ...args, __request_id: request_id }));
      }
      catch (error)
      {
        this.finish_request(request, finish({ ok: false, error: `engine write failed, ${error.message}` }, "write"));
        this.close();
      }
    });
  }

  async ensure_connected(timeout_ms = this.timeout_ms) {
    this.cancel_idle_close();
    if (this.socket && !this.socket.destroyed && !this.socket.writableEnded)
    {
      return true;
    }

    if (this.connecting)
    {
      return this.connecting;
    }

    this.connecting = new Promise((resolve) => {
      const socket = net.createConnection({ host: this.host, port: this.port });
      this.socket = socket;
      this.buffer = "";
      let settled = false;

      const finish = (ok) => {
        if (settled)
        {
          return;
        }

        settled = true;
        clearTimeout(connect_timer);
        this.connecting = null;
        resolve(ok);
      };

      const connect_timer = setTimeout(() => {
        socket.destroy();
        this.socket = null;
        finish(false);
      }, timeout_ms);
      connect_timer.unref?.();

      socket.on("connect", () => {
        finish(true);
      });

      socket.on("data", (chunk) => {
        this.handle_data(chunk.toString("utf8"));
      });

      socket.on("error", (error) => {
        this.fail_all(`engine connection failed, ${error.message}`);
        finish(false);
      });

      socket.on("close", () => {
        this.fail_all("engine connection closed");
        if (this.socket === socket)
        {
          this.socket = null;
        }
        finish(false);
      });
    });

    return this.connecting;
  }

  handle_data(text) {
    this.buffer += text;

    let newline = this.buffer.indexOf("\n");
    while (newline !== -1)
    {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      let parsed = null;
      try
      {
        parsed = JSON.parse(line);
      }
      catch (error)
      {
        const request = this.pending.shift();
        if (request)
        {
          this.pending_by_request_id.delete(request.request_id);
          this.resolve_request(request, request.finish({ ok: false, error: `invalid engine response, ${error.message}` }, "parse"));
        }
        newline = this.buffer.indexOf("\n");
        continue;
      }

      let request = null;
      if (typeof parsed?.request_id === "string")
      {
        request = this.pending_by_request_id.get(parsed.request_id) ?? null;
        if (!request)
        {
          newline = this.buffer.indexOf("\n");
          continue;
        }
        this.remove_pending_request(request);
      }
      else
      {
        request = this.pending.shift();
        if (request)
        {
          this.pending_by_request_id.delete(request.request_id);
        }
      }

      if (request)
      {
        if (request.timed_out)
        {
          clearTimeout(request.timer);
          newline = this.buffer.indexOf("\n");
          continue;
        }

        this.resolve_request(request, request.finish(parsed));
      }

      if (this.pending.length === 0)
      {
        this.schedule_idle_close();
      }

      newline = this.buffer.indexOf("\n");
    }
  }

  resolve_request(request, result) {
    if (request.completed)
    {
      return;
    }

    request.completed = true;
    clearTimeout(request.timer);
    request.resolve(result);
  }

  finish_request(request, result) {
    this.remove_pending_request(request);
    this.resolve_request(request, result);
  }

  remove_pending_request(request) {
    const index = this.pending.indexOf(request);
    if (index !== -1)
    {
      this.pending.splice(index, 1);
    }

    this.pending_by_request_id.delete(request.request_id);
  }

  fail_all(message) {
    const requests = this.pending.splice(0);
    this.pending_by_request_id.clear();
    for (const request of requests)
    {
      this.resolve_request(request, request.finish({ ok: false, error: message }, "socket"));
    }
  }

  close() {
    this.cancel_idle_close();
    if (this.socket && !this.socket.destroyed)
    {
      this.socket.destroy();
    }
    this.socket = null;
  }

  schedule_idle_close() {
    this.cancel_idle_close();
    this.idle_close_timer = setTimeout(() => {
      this.idle_close_timer = null;
      if (this.pending.length !== 0 || !this.socket || this.socket.destroyed)
      {
        return;
      }

      this.socket.end();
    }, 250);
    this.idle_close_timer.unref?.();
  }

  cancel_idle_close() {
    if (this.idle_close_timer)
    {
      clearTimeout(this.idle_close_timer);
      this.idle_close_timer = null;
    }
  }
}
