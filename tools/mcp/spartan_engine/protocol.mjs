/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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

// absent means enabled, an older editor build that does not send the flag still gets enrichment
function parse_enrich(params) {
  const value = params.get("enrich");
  if (value === null) {
    return true;
  }
  return !["0", "false", "no", "off"].includes(
    value.trim().toLowerCase(),
  );
}

export function parse_prompt_payload(value) {
  const params = new URLSearchParams(value);
  const prompt = params.get("prompt");
  const images = params
    .getAll("image")
    .map((item) => String(item ?? "").trim())
    .filter(Boolean)
    .slice(0, 5);
  if (prompt !== null) {
    return {
      prompt,
      api_key: (params.get("api_key") ?? process.env.CURSOR_API_KEY ?? "").trim(),
      model_id: (params.get("model") ?? "auto").trim() || "auto",
      enrich: parse_enrich(params),
      images,
    };
  }

  return {
    prompt: decode_value(value),
    api_key: (process.env.CURSOR_API_KEY ?? "").trim(),
    model_id: "auto",
    enrich: true,
    images,
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
