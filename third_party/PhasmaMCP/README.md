# PhasmaMCP

Engine-agnostic C++ library implementing the **Model Context Protocol (MCP)** server side of the
[2025-06-18 spec](https://modelcontextprotocol.io/specification/2025-06-18). Drop into any C++ engine
or CLI to stand up a working MCP server — register tool handlers, pick a transport, and you're done.

## Layout

```
PhasmaMCP/
  include/PhasmaMCP/
    LogLevel.h            severity-tagged log callback (Debug/Info/Warn/Error)
    Tool.h                CallToolResult, Context, ToolDefinition (JSON Schema in/out)
    Server.h              JSON-RPC dispatch (initialize / tools/list / tools/call / ping)
    HttpTransport.h       httplib-backed HTTP transport (loopback OAuth shim, Origin gate)
    Utils.h               JSON helpers, Base64, UTF-8 sanitize, RGBA->PNG, path-safety check
    Codebase/             optional, gated by PE_PMCP_CODEBASE CMake option (default ON)
      BM25Index.h         thread-safe Okapi BM25 index (camelCase / snake_case tokenization)
      CodebaseIndexer.h   directory walker + line-chunked feeder for BM25Index
      CodebaseContext.h   owns the index + indexing config + status
  src/
    Server.cpp
    HttpTransport.cpp
    BM25Index.cpp         } compiled when PE_PMCP_CODEBASE=ON
    CodebaseIndexer.cpp   }
    CodebaseContext.cpp   }
  third_party/
    httplib/              vendored single-header HTTP server
    nlohmann/             vendored JSON
```

Everything lives in `namespace pmcp`. The `Codebase/` headers are an **optional module** inside the
same target — they share dependencies with the protocol core (no engine coupling, no scripting, no
renderer) but are unrelated to JSON-RPC dispatch. Set `-DPE_PMCP_CODEBASE=OFF` to strip them from
the build for minimal protocol-only deployments.

## Architecture

- **`pmcp::Server`** is transport-agnostic. It owns JSON-RPC dispatch, lifecycle, capabilities, and
  tool registration. It does **not** open a socket — transports do.
- **`pmcp::HttpTransport`** is one transport. It wraps `httplib::Server` and forwards every request
  body to `pmcp::Server::HandleMessage`. Adding stdio later is a new file, not a redesign.
- **`pmcp::ToolDefinition`** carries `name`, `title`, `description`, an arbitrary JSON-Schema
  `inputSchema` / `outputSchema`, optional `annotations`, and a handler with signature
  `(const nlohmann::json& args, pmcp::Context& ctx) -> pmcp::CallToolResult`.
- **`pmcp::CallToolResult`** mirrors the spec's `CallToolResult`: `content` array, optional
  `structuredContent`, `isError`. Helpers: `Text`, `Json`, `Error`, `ImageBase64`.
- **`pmcp::Context`** is per-call: cancellation hook, progress reporter, severity-tagged log callback.
  Transports/hosts populate it as their capabilities allow.
- **`pmcp::BM25Index`** + **`CodebaseIndexer`** (optional) — drop into a tool catalog that wants to
  expose `find_symbol` / `search_codebase` / `grep_project`-style tools.

## Minimal usage

```cpp
#include <PhasmaMCP/Server.h>
#include <PhasmaMCP/HttpTransport.h>

pmcp::ServerConfig cfg;
cfg.name = "my-engine";
cfg.title = "My Engine MCP";
cfg.log = [](pmcp::LogLevel lvl, const std::string& msg) { /* route to your logger */ };

pmcp::Server server(cfg);
server.SetTools({
    pmcp::ToolDefinition{
        .name = "echo",
        .description = "Echo input back as text",
        .inputSchema = pmcp::schema::Object({
            {"text", "Text to echo", pmcp::schema::String(), /*required*/true},
        }),
        .handler = [](const nlohmann::json& args, pmcp::Context&) {
            return pmcp::CallToolResult::Text(args.value("text", ""));
        },
    },
});

pmcp::HttpTransportConfig http;
http.port = 8765;
http.enableLocalOauthShim = true;  // turn on if Claude Code or Claude Desktop will connect
pmcp::HttpTransport transport(&server, http);
transport.Start();
```

`SetTools` snapshots a static list. `SetToolProvider` is the canonical alternative that rebuilds the
list on every dispatch — use it when tool availability depends on engine state.

## Spec compliance (2025-06-18)

- Reports `protocolVersion: "2025-06-18"` from `initialize`.
- Sends `MCP-Protocol-Version` HTTP header on every response after `initialize`.
- Returns the server's protocol version regardless of what the client requested (per spec).
- **Does not** support JSON-RPC batching — the spec dropped it; array payloads are rejected with
  `-32600 Invalid Request`.
- Tool execution errors return `isError=true` inside a normal JSON-RPC `result`, **not** as
  JSON-RPC errors (per spec). JSON-RPC errors are reserved for protocol-level failures.

## Auth posture

The OAuth shim (`enableLocalOauthShim`, default OFF) publishes RFC 8414 metadata, dynamic
registration, authorize, and token endpoints purely so MCP clients (e.g. Claude Code) that probe
for OAuth stop prompting "Needs Auth". The bearer token is **static and non-secret** by design.

Real safety comes from two structural invariants enforced by the transport:

1. `Start()` refuses to bind to a non-loopback address while the shim is enabled — the phantom
   auth surface can never be reached from off-host.
2. `/mcp` and `/tool` reject requests with a non-local `Origin` header (browser-CSRF defense).

Hosts that bind beyond loopback must keep the shim **off** and put the transport behind their own
auth (reverse proxy, mTLS, etc.).

## Build

```cmake
add_subdirectory(PhasmaMCP)                       # default: codebase module included
# add_subdirectory(PhasmaMCP) with -DPE_PMCP_CODEBASE=OFF for protocol-only builds
target_link_libraries(MyApp PRIVATE PhasmaMCP)
```

Dependencies: `Threads::Threads`, and on Windows `ws2_32`. No external network or crypto deps —
`httplib` is plain HTTP; HTTPS termination is the host's responsibility.

## What it does NOT provide

PhasmaMCP is **only** the server side of MCP. There is no LLM client, no provider backends, no
conversational chat, no agent loop. Use any external MCP client (Claude Code, Claude Desktop,
mcp-remote bridge, OpenSpace) to drive the tools your host exposes.
