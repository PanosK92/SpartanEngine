#pragma once

#include "PhasmaMCP/LogLevel.h"
#include "PhasmaMCP/Tool.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace pmcp
{
    // Identification + protocol parameters returned to clients on `initialize`.
    struct ServerConfig
    {
        std::string name = "phasma-mcp";
        std::string title;
        std::string version = "0.1.0";
        std::string instructions;
        std::string protocolVersion = "2025-06-18";
        LogCallback log;
    };

    // Lazy provider rebuilt on every `tools/list` / `tools/call`. Use this when tool availability
    // depends on engine state (e.g. show codebase tools only once the index is ready).
    using ToolListProvider = std::function<std::vector<ToolDefinition>()>;

    // Transport-agnostic MCP protocol core. Owns:
    //   - JSON-RPC dispatch for `initialize` / `ping` / `tools/list` / `tools/call`
    //   - tool registry (static or via provider)
    //   - structured logging
    // Does NOT own a transport; instead, transports call `HandleMessage` per request.
    class Server
    {
    public:
        explicit Server(ServerConfig config);

        // Canonical: per-request tool list. Invoked once per `tools/list` and `tools/call`.
        void SetToolProvider(ToolListProvider provider);

        // One-shot wrapper: snapshot a static list. Internally forwards to SetToolProvider.
        void SetTools(std::vector<ToolDefinition> tools);

        // Process a single JSON-RPC message. Returns a response message (or std::nullopt for
        // notifications and unrecognized messages without an id). Caller is responsible for
        // serialization + transport-level framing.
        std::optional<nlohmann::json> HandleMessage(const nlohmann::json &message,
                                                    const std::string &clientLabel);

        // Read-only access for transports.
        const ServerConfig &Config() const { return m_config; }

        // Log via the configured callback (no-op when unset).
        void Log(LogLevel level, const std::string &message) const;

    private:
        std::optional<nlohmann::json> HandleInitialize(const nlohmann::json &id, const nlohmann::json &params,
                                                       const std::string &clientLabel);
        std::optional<nlohmann::json> HandleToolsList(const nlohmann::json &id);
        std::optional<nlohmann::json> HandleToolsCall(const nlohmann::json &id, const nlohmann::json &params,
                                                      const std::string &clientLabel);

        ServerConfig m_config;
        ToolListProvider m_provider;
    };

    // JSON-RPC builders exposed for transports that need to emit standalone responses
    // (e.g. parse errors before a message can be dispatched).
    nlohmann::json MakeJsonRpcResult(const nlohmann::json &id, nlohmann::json result);
    nlohmann::json MakeJsonRpcError(const nlohmann::json &id, int code, const std::string &message);

    // Stable error codes used by the dispatcher (subset of the JSON-RPC 2.0 reserved range).
    constexpr int kJsonRpcParseError = -32700;
    constexpr int kJsonRpcInvalidRequest = -32600;
    constexpr int kJsonRpcMethodNotFound = -32601;
    constexpr int kJsonRpcInvalidParams = -32602;
    constexpr int kJsonRpcInternalError = -32603;
} // namespace pmcp
