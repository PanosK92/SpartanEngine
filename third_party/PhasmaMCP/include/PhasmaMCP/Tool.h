#pragma once

#include "PhasmaMCP/LogLevel.h"

#include <functional>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pmcp
{
    // Result of a tool invocation, mirroring the MCP `CallToolResult` shape.
    // `content` is an array of content parts (text/image/resource_link/...).
    // `structuredContent` is the optional machine-readable form validated against `outputSchema`.
    struct CallToolResult
    {
        nlohmann::json content = nlohmann::json::array();
        nlohmann::json structuredContent;
        bool isError = false;

        // Plain text result (single text content part, no structured form).
        static CallToolResult Text(std::string text)
        {
            CallToolResult r;
            r.content.push_back({{"type", "text"}, {"text", std::move(text)}});
            return r;
        }

        // Structured JSON result. Mirrors the value as a stringified text part for clients
        // that ignore `structuredContent` (per MCP spec recommendation).
        static CallToolResult Json(nlohmann::json value)
        {
            CallToolResult r;
            r.content.push_back({
                {"type", "text"},
                {"text", value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)},
            });
            r.structuredContent = std::move(value);
            return r;
        }

        // Error result. The MCP spec routes execution errors via `isError=true` (NOT JSON-RPC error).
        static CallToolResult Error(const std::string &message)
        {
            CallToolResult r;
            r.content.push_back({{"type", "text"}, {"text", message}});
            r.structuredContent = {{"error", message}};
            r.isError = true;
            return r;
        }

        // Image content part (base64 PNG / JPEG / etc.).
        static CallToolResult ImageBase64(std::string base64Data, std::string mimeType)
        {
            CallToolResult r;
            r.content.push_back({
                {"type", "image"},
                {"data", std::move(base64Data)},
                {"mimeType", std::move(mimeType)},
            });
            return r;
        }
    };

    // Per-call context passed to every tool handler. Carries cancellation, progress, and logging
    // so engines can plug their own scheduling/observability without changing the handler signature.
    // The fields default to harmless no-ops; transports/hosts populate them as capabilities allow.
    struct Context
    {
        std::function<bool()> isCancelled = []
        { return false; };
        std::function<void(double progress, const std::string &note)> reportProgress;
        LogCallback log = [](LogLevel, const std::string &) {};
    };

    using ToolHandler = std::function<CallToolResult(const nlohmann::json &args, Context &ctx)>;

    // An MCP-exposed tool. `inputSchema` is an arbitrary JSON Schema (object type by convention);
    // `outputSchema` is optional and validates `structuredContent` when present.
    struct ToolDefinition
    {
        std::string name;
        std::string title;
        std::string description;
        nlohmann::json inputSchema = nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        nlohmann::json outputSchema;
        nlohmann::json annotations;
        ToolHandler handler;
    };

    // Compact JSON Schema builders. Optional sugar — handlers may also assign `inputSchema` directly.
    namespace schema
    {
        struct Property
        {
            std::string name;
            std::string description;
            nlohmann::json type;
            bool required = false;
        };

        inline nlohmann::json String()
        {
            return nlohmann::json{{"type", "string"}};
        }
        inline nlohmann::json Integer()
        {
            return nlohmann::json{{"type", "integer"}};
        }
        inline nlohmann::json Number()
        {
            return nlohmann::json{{"type", "number"}};
        }
        inline nlohmann::json Boolean()
        {
            return nlohmann::json{{"type", "boolean"}};
        }

        inline nlohmann::json ArrayOf(nlohmann::json itemSchema)
        {
            return nlohmann::json{{"type", "array"}, {"items", std::move(itemSchema)}};
        }

        inline nlohmann::json Enum(std::initializer_list<const char *> values)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto *v : values)
                arr.push_back(v);
            return nlohmann::json{{"type", "string"}, {"enum", std::move(arr)}};
        }

        inline nlohmann::json Object(std::initializer_list<Property> properties)
        {
            nlohmann::json schema = {{"type", "object"}, {"properties", nlohmann::json::object()}};
            nlohmann::json required = nlohmann::json::array();
            for (const auto &p : properties)
            {
                nlohmann::json sub = p.type;
                if (!p.description.empty())
                    sub["description"] = p.description;
                schema["properties"][p.name] = std::move(sub);
                if (p.required)
                    required.push_back(p.name);
            }
            if (!required.empty())
                schema["required"] = std::move(required);
            return schema;
        }
    } // namespace schema
} // namespace pmcp
