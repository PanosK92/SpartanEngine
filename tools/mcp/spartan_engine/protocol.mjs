export function encode_value(value) {
  return encodeURIComponent(String(value));
}

export function decode_value(value) {
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}

export function parse_line(line) {
  const separator = line.indexOf(" ");
  if (separator === -1) {
    return { command: line, value: "" };
  }

  return {
    command: line.slice(0, separator),
    value: line.slice(separator + 1),
  };
}

export function parse_prompt_payload(value) {
  const params = new URLSearchParams(value);
  const prompt = params.get("prompt");
  if (prompt !== null) {
    return {
      prompt,
      api_key: (params.get("api_key") ?? process.env.CURSOR_API_KEY ?? "").trim(),
      model_id: (params.get("model") ?? "auto").trim() || "auto",
    };
  }

  return {
    prompt: decode_value(value),
    api_key: (process.env.CURSOR_API_KEY ?? "").trim(),
    model_id: "auto",
  };
}

export function parse_key_payload(value) {
  const params = new URLSearchParams(value);
  return (params.get("api_key") ?? process.env.CURSOR_API_KEY ?? "").trim();
}

export function send_line(socket, status, text) {
  if (socket.destroyed) {
    return;
  }

  try {
    socket.write(`${status} ${encode_value(text)}\n`);
  } catch {
  }
}

export function send_event(socket, event) {
  send_line(socket, "event", JSON.stringify(event));
}

export function make_run_id() {
  return `run_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
}
