#include "PhasmaMCP/Server.h"

#include <utility>

namespace pmcp
{
    namespace
    {
        const ToolDefinition *FindTool(const std::vector<ToolDefinition> &tools, const std::string &name)
        {
            for (const auto &tool : tools)
            {
                if (tool.name == name)
                    return &tool;
            }
            return nullptr;
        }

        // Builds the wire form of a single tool for `tools/list`. Mirrors the MCP 2025-06-18 spec:
        // name + (optional) title + description + inputSchema + (optional) outputSchema + (optional) annotations.
        nlohmann::json BuildToolWire(const ToolDefinition &tool)
        {
            nlohmann::json out = {
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.inputSchema.is_null() ? nlohmann::json{{"type", "object"}} : tool.inputSchema},
            };
            if (!tool.title.empty())
                out["title"] = tool.title;
            if (!tool.outputSchema.is_null() && !tool.outputSchema.empty())
                out["outputSchema"] = tool.outputSchema;
            if (!tool.annotations.is_null() && !tool.annotations.empty())
                out["annotations"] = tool.annotations;
            return out;
        }

        nlohmann::json BuildToolResultWire(const CallToolResult &result)
        {
            nlohmann::json out = {
                {"content", result.content.is_null() ? nlohmann::json::array() : result.content},
                {"isError", result.isError},
            };
            if (!result.structuredContent.is_null())
                out["structuredContent"] = result.structuredContent;
            return out;
        }
    } // namespace

    nlohmann::json MakeJsonRpcResult(const nlohmann::json &id, nlohmann::json result)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::move(result)},
        };
    }

    nlohmann::json MakeJsonRpcError(const nlohmann::json &id, int code, const std::string &message)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", message}}},
        };
    }

    Server::Server(ServerConfig config)
        : m_config(std::move(config))
    {
    }

    void Server::SetToolProvider(ToolListProvider provider)
    {
        m_provider = std::move(provider);
    }

    void Server::SetTools(std::vector<ToolDefinition> tools)
    {
        // SetTools is a one-shot wrapper around the canonical SetToolProvider — snapshot once,
        // serve the same list on every dispatch.
        auto shared = std::make_shared<std::vector<ToolDefinition>>(std::move(tools));
        m_provider = [shared]()
        { return *shared; };
    }

    void Server::Log(LogLevel level, const std::string &message) const
    {
        if (m_config.log)
            m_config.log(level, message);
    }

    std::optional<nlohmann::json> Server::HandleMessage(const nlohmann::json &message,
                                                        const std::string &clientLabel)
    {
        if (!message.is_object())
            return MakeJsonRpcError(nullptr, kJsonRpcInvalidRequest, "Invalid Request");

        const bool hasId = message.contains("id");
        const nlohmann::json id = hasId ? message["id"] : nlohmann::json(nullptr);
        const std::string method = message.value("method", "");

        if (method.empty())
            return hasId ? std::optional<nlohmann::json>(MakeJsonRpcError(id, kJsonRpcInvalidRequest, "Invalid Request"))
                         : std::nullopt;

        // notifications/* never produce responses
        if (method.rfind("notifications/", 0) == 0)
            return std::nullopt;

        const auto &params = message.contains("params") && message["params"].is_object()
                                 ? message["params"]
                                 : nlohmann::json::object();

        if (method == "initialize")
            return HandleInitialize(id, params, clientLabel);
        if (method == "ping")
            return MakeJsonRpcResult(id, nlohmann::json::object());
        if (method == "tools/list")
            return HandleToolsList(id);
        if (method == "tools/call")
            return HandleToolsCall(id, params, clientLabel);

        return hasId ? std::optional<nlohmann::json>(MakeJsonRpcError(id, kJsonRpcMethodNotFound, "Method not found: " + method))
                     : std::nullopt;
    }

    std::optional<nlohmann::json> Server::HandleInitialize(const nlohmann::json &id, const nlohmann::json &params,
                                                           const std::string &clientLabel)
    {
        // Spec: server echoes its own protocolVersion regardless of what the client requested.
        const std::string clientName = params.contains("clientInfo") && params["clientInfo"].is_object()
                                           ? params["clientInfo"].value("name", std::string{"unknown"})
                                           : std::string{"unknown"};
        Log(LogLevel::Info, "[MCP] " + clientName + " connected (" + clientLabel + ")");

        nlohmann::json serverInfo = {
            {"name", m_config.name},
            {"version", m_config.version},
        };
        if (!m_config.title.empty())
            serverInfo["title"] = m_config.title;

        nlohmann::json result = {
            {"protocolVersion", m_config.protocolVersion},
            {"capabilities", {{"tools", nlohmann::json::object()}}},
            {"serverInfo", std::move(serverInfo)},
        };
        if (!m_config.instructions.empty())
            result["instructions"] = m_config.instructions;

        return MakeJsonRpcResult(id, std::move(result));
    }

    std::optional<nlohmann::json> Server::HandleToolsList(const nlohmann::json &id)
    {
        auto tools = m_provider ? m_provider() : std::vector<ToolDefinition>{};
        nlohmann::json wire = nlohmann::json::array();
        wire.get_ptr<nlohmann::json::array_t *>()->reserve(tools.size());
        for (const auto &tool : tools)
            wire.push_back(BuildToolWire(tool));
        return MakeJsonRpcResult(id, nlohmann::json{{"tools", std::move(wire)}});
    }

    std::optional<nlohmann::json> Server::HandleToolsCall(const nlohmann::json &id, const nlohmann::json &params,
                                                          const std::string &clientLabel)
    {
        const std::string toolName = params.value("name", "");
        const nlohmann::json args = params.contains("arguments") && params["arguments"].is_object()
                                        ? params["arguments"]
                                        : nlohmann::json::object();

        Log(LogLevel::Info, "[MCP] [" + clientLabel + "] -> " + toolName);

        auto tools = m_provider ? m_provider() : std::vector<ToolDefinition>{};
        const auto *tool = FindTool(tools, toolName);
        if (!tool)
            return MakeJsonRpcError(id, kJsonRpcInvalidParams, "Unknown tool: " + toolName);
        if (!tool->handler)
            return MakeJsonRpcError(id, kJsonRpcInternalError, "Tool has no handler: " + toolName);

        Context ctx;
        ctx.log = m_config.log ? m_config.log : Context{}.log;

        CallToolResult result;
        try
        {
            result = tool->handler(args, ctx);
        }
        catch (const std::exception &ex)
        {
            Log(LogLevel::Error, std::string("[MCP] tool '") + toolName + "' threw: " + ex.what());
            result = CallToolResult::Error(std::string("tool exception: ") + ex.what());
        }
        catch (...)
        {
            Log(LogLevel::Error, std::string("[MCP] tool '") + toolName + "' threw unknown exception");
            result = CallToolResult::Error("tool exception: unknown");
        }

        Log(result.isError ? LogLevel::Warn : LogLevel::Info,
            std::string("[MCP] [") + clientLabel + "] <- " + toolName + (result.isError ? ": error" : ": ok"));

        return MakeJsonRpcResult(id, BuildToolResultWire(result));
    }
} // namespace pmcp
