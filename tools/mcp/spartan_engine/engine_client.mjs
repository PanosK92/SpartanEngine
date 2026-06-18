import net from "node:net";

function protocol_value(value) {
  if (Array.isArray(value)) {
    return value.join(",");
  }

  if (typeof value === "boolean") {
    return value ? "true" : "false";
  }

  return String(value);
}

function command_line(command, args = {}) {
  const parts = [command];
  for (const [key, value] of Object.entries(args)) {
    if (value === undefined || value === null) {
      continue;
    }

    parts.push(`${key}=${encodeURIComponent(protocol_value(value))}`);
  }

  return `${parts.join(" ")}\n`;
}

export class EngineClient {
  constructor({ host, port, timeout_ms }) {
    this.host = host;
    this.port = port;
    this.timeout_ms = timeout_ms;
    this.socket = null;
    this.buffer = "";
    this.pending = [];
    this.connecting = null;
  }

  async command(command, args = {}, timeout_ms = this.timeout_ms) {
    await this.ensure_connected();
    if (!this.socket || this.socket.destroyed) {
      return { ok: false, error: "engine connection is not available" };
    }

    return new Promise((resolve) => {
      const request = {
        resolve,
        completed: false,
        timed_out: false,
        timer: setTimeout(() => {
          request.timed_out = true;
          this.resolve_request(request, {
            ok: false,
            error: `engine request timed out after ${timeout_ms}ms`,
            code: "engine_timeout",
            retryable: true,
            suggested_action: "retry once, then use a smaller operation or a native batch command",
          });
        }, timeout_ms),
      };

      this.pending.push(request);
      try {
        this.socket.write(command_line(command, args));
      } catch (error) {
        this.finish_request(request, { ok: false, error: `engine write failed, ${error.message}` });
        this.close();
      }
    });
  }

  async ensure_connected() {
    if (this.socket && !this.socket.destroyed) {
      return;
    }

    if (this.connecting) {
      return this.connecting;
    }

    this.connecting = new Promise((resolve) => {
      const socket = net.createConnection({ host: this.host, port: this.port });
      this.socket = socket;
      this.buffer = "";

      socket.on("connect", () => {
        this.connecting = null;
        resolve();
      });

      socket.on("data", (chunk) => {
        this.handle_data(chunk.toString("utf8"));
      });

      socket.on("error", (error) => {
        this.fail_all(`engine connection failed, ${error.message}`);
        this.connecting = null;
        resolve();
      });

      socket.on("close", () => {
        this.fail_all("engine connection closed");
        this.socket = null;
        this.connecting = null;
      });
    });

    return this.connecting;
  }

  handle_data(text) {
    this.buffer += text;

    let newline = this.buffer.indexOf("\n");
    while (newline !== -1) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      const request = this.pending.shift();
      if (request) {
        if (request.timed_out) {
          clearTimeout(request.timer);
          newline = this.buffer.indexOf("\n");
          continue;
        }

        try {
          this.resolve_request(request, JSON.parse(line));
        } catch (error) {
          this.resolve_request(request, { ok: false, error: `invalid engine response, ${error.message}` });
        }
      }

      newline = this.buffer.indexOf("\n");
    }
  }

  resolve_request(request, result) {
    if (request.completed) {
      return;
    }

    request.completed = true;
    clearTimeout(request.timer);
    request.resolve(result);
  }

  finish_request(request, result) {
    const index = this.pending.indexOf(request);
    if (index !== -1) {
      this.pending.splice(index, 1);
    }

    this.resolve_request(request, result);
  }

  fail_all(message) {
    const requests = this.pending.splice(0);
    for (const request of requests) {
      this.resolve_request(request, { ok: false, error: message });
    }
  }

  close() {
    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
    }
    this.socket = null;
  }
}
