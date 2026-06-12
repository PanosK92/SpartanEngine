#include "PhasmaMCP/HttpTransport.h"

#include "PhasmaMCP/Server.h"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

namespace pmcp
{
    namespace
    {
        std::string DumpJson(const nlohmann::json &j)
        {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        bool IsLocalOrigin(const std::string &origin)
        {
            if (origin.empty())
                return true;
            std::string host = origin;
            const auto schemeEnd = host.find("://");
            if (schemeEnd != std::string::npos)
                host = host.substr(schemeEnd + 3);
            const auto portOrPath = host.find_first_of(":/");
            if (portOrPath != std::string::npos)
                host = host.substr(0, portOrPath);
            return host == "localhost" || host == "127.0.0.1" || host == "[::1]" || host == "::1";
        }

        // True if `address` is a loopback bind. Accepts the canonical literals plus the IPv4
        // 127.0.0.0/8 range (127.x.y.z). Used as a hard prerequisite for enabling the OAuth shim.
        bool IsLoopbackBind(const std::string &address)
        {
            if (address.empty())
                return false;
            if (address == "localhost" || address == "::1" || address == "[::1]")
                return true;
            if (address.size() >= 4 && address.compare(0, 4, "127.") == 0)
                return true;
            return false;
        }
    } // namespace

    HttpTransport::HttpTransport(Server *server, HttpTransportConfig config)
        : m_server(server), m_config(std::move(config)), m_http(std::make_unique<httplib::Server>())
    {
    }

    HttpTransport::~HttpTransport()
    {
        Stop();
    }

    void HttpTransport::Log(LogLevel level, const std::string &message) const
    {
        if (m_config.log)
            m_config.log(level, message);
    }

    void HttpTransport::ConfigureRoutes()
    {
        if (!m_http || !m_server)
            return;

        const std::string &protocolVersion = m_server->Config().protocolVersion;

        if (m_config.enableLocalOauthShim)
        {
            // OAuth 2.0 metadata + register/token endpoints. Static credentials valid only for local
            // 127.0.0.1 use; the shim exists so MCP clients that probe for OAuth (e.g. Claude Code)
            // stop prompting for "Needs Auth" on every launch.
            m_http->Get("/.well-known/oauth-authorization-server",
                        [this](const httplib::Request &, httplib::Response &res)
                        {
                            const std::string base = "http://127.0.0.1:" + std::to_string(m_config.port);
                            res.set_content(DumpJson(nlohmann::json{
                                                {"issuer", base},
                                                {"authorization_endpoint", base + "/oauth/authorize"},
                                                {"token_endpoint", base + "/oauth/token"},
                                                {"registration_endpoint", base + "/oauth/register"},
                                                {"token_endpoint_auth_methods_supported", nlohmann::json::array({"none"})},
                                                {"grant_types_supported", nlohmann::json::array({"authorization_code", "client_credentials"})},
                                                {"response_types_supported", nlohmann::json::array({"code", "token"})},
                                                {"code_challenge_methods_supported", nlohmann::json::array({"S256", "plain"})},
                                            }),
                                            "application/json");
                        });

            m_http->Get("/oauth/authorize", [](const httplib::Request &req, httplib::Response &res)
                        {
                            const std::string redirectUri = req.get_param_value("redirect_uri");
                            const std::string state = req.get_param_value("state");
                            if (redirectUri.empty())
                            {
                                res.status = 400;
                                res.set_content(DumpJson(nlohmann::json{{"error", "missing redirect_uri"}}), "application/json");
                                return;
                            }
                            std::string location = redirectUri + (redirectUri.find('?') == std::string::npos ? "?" : "&");
                            location += "code=phasma-mcp-local-auth-code";
                            if (!state.empty())
                                location += "&state=" + state;
                            res.status = 302;
                            res.set_header("Location", location); });

            m_http->Post("/oauth/register", [](const httplib::Request &req, httplib::Response &res)
                         {
                             nlohmann::json regBody;
                             try
                             {
                                 regBody = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
                             }
                             catch (...)
                             {
                                 regBody = nlohmann::json::object();
                             }

                             nlohmann::json redirectUris = regBody.contains("redirect_uris") && regBody["redirect_uris"].is_array()
                                                              ? regBody["redirect_uris"]
                                                              : nlohmann::json::array({"http://127.0.0.1/callback"});

                             res.status = 201;
                             res.set_content(DumpJson(nlohmann::json{
                                                 {"client_id", "phasma-mcp-local-client"},
                                                 {"client_secret", ""},
                                                 {"redirect_uris", redirectUris},
                                                 {"token_endpoint_auth_method", "none"},
                                                 {"grant_types", nlohmann::json::array({"client_credentials"})},
                                             }),
                                             "application/json"); });

            const std::string oauthToken = m_config.staticBearerToken;
            m_http->Post("/oauth/token", [oauthToken](const httplib::Request &, httplib::Response &res)
                         { res.set_content(DumpJson(nlohmann::json{
                                               {"access_token", oauthToken},
                                               {"token_type", "Bearer"},
                                               {"expires_in", 315360000},
                                           }),
                                           "application/json"); });
        }

        // Browser-CSRF defense for /mcp and /tool: when a request carries a non-empty `Origin`
        // header from a non-local page (e.g. a malicious site that knows we're on 127.0.0.1),
        // refuse to dispatch. The transport binds loopback-only so this is the only realistic
        // attack surface; we do NOT pretend to authenticate the caller beyond that — the shim's
        // bearer token is not a secret and validating it would be theater (anyone with local
        // code execution can fetch /oauth/token themselves). The shim exists to satisfy MCP
        // clients (Claude Code) that probe for OAuth metadata, not to gate access.
        auto enforceLocalOrigin = [](const httplib::Request &req, httplib::Response &res) -> bool
        {
            if (IsLocalOrigin(req.get_header_value("Origin")))
                return true;
            res.status = 403;
            res.set_content(DumpJson(nlohmann::json{{"error", "forbidden origin"}}), "application/json");
            return false;
        };

        m_http->Get("/mcp", [](const httplib::Request &, httplib::Response &res)
                    {
                        res.status = 405;
                        res.set_header("Allow", "POST");
                        res.set_content(DumpJson(nlohmann::json{{"error", "Method Not Allowed - use POST /mcp"}}), "application/json"); });

        m_http->Post("/mcp", [this, protocolVersion, enforceLocalOrigin](const httplib::Request &req, httplib::Response &res)
                     {
                         res.set_header("MCP-Protocol-Version", protocolVersion);

                         if (!enforceLocalOrigin(req, res))
                             return;

                         const std::string userAgent = req.get_header_value("User-Agent");
                         const std::string clientLabel = userAgent.empty() ? std::string{"unknown"} : userAgent;

                         nlohmann::json body;
                         try
                         {
                             body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
                         }
                         catch (...)
                         {
                             res.set_content(DumpJson(MakeJsonRpcError(nullptr, kJsonRpcParseError, "Parse error")), "application/json");
                             return;
                         }

                         // MCP 2025-06-18 dropped JSON-RPC batching. Reject array payloads explicitly so callers see
                         // the upgrade requirement instead of silent half-support.
                         if (body.is_array())
                         {
                             res.status = 400;
                             res.set_content(DumpJson(MakeJsonRpcError(nullptr, kJsonRpcInvalidRequest,
                                                                      "JSON-RPC batching is not supported (MCP 2025-06-18)")),
                                             "application/json");
                             return;
                         }

                         auto response = m_server->HandleMessage(body, clientLabel);
                         if (!response.has_value())
                         {
                             res.status = 202;
                             return;
                         }
                         res.set_content(DumpJson(*response), "application/json"); });

        m_http->Get("/health", [this](const httplib::Request &, httplib::Response &res)
                    { res.set_content(DumpJson(nlohmann::json{
                                          {"ok", true},
                                          {"service", m_server ? m_server->Config().name : std::string{"phasma-mcp"}},
                                          {"port", m_config.port},
                                          {"protocolVersion", m_server ? m_server->Config().protocolVersion : std::string{}},
                                      }),
                                      "application/json"); });

        // Convenience GET /tools — same content as MCP tools/list but without JSON-RPC framing.
        m_http->Get("/tools", [this, protocolVersion](const httplib::Request &, httplib::Response &res)
                    {
                        res.set_header("MCP-Protocol-Version", protocolVersion);
                        const nlohmann::json fakeId = 1;
                        const nlohmann::json fakeRequest = {
                            {"jsonrpc", "2.0"},
                            {"id", fakeId},
                            {"method", "tools/list"},
                        };
                        auto response = m_server->HandleMessage(fakeRequest, "http-tools");
                        if (response.has_value() && (*response).contains("result"))
                            res.set_content(DumpJson((*response)["result"]), "application/json");
                        else
                            res.set_content(DumpJson(nlohmann::json{{"tools", nlohmann::json::array()}}), "application/json"); });

        // Convenience POST /tool — bare tool call without JSON-RPC framing. Same Origin gate as /mcp.
        m_http->Post("/tool", [this, protocolVersion, enforceLocalOrigin](const httplib::Request &req, httplib::Response &res)
                     {
                         res.set_header("MCP-Protocol-Version", protocolVersion);

                         if (!enforceLocalOrigin(req, res))
                             return;

                         nlohmann::json body;
                         try
                         {
                             body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
                         }
                         catch (...)
                         {
                             res.status = 400;
                             res.set_content(DumpJson(nlohmann::json{{"error", "invalid json body"}}), "application/json");
                             return;
                         }

                         const nlohmann::json fakeRequest = {
                             {"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "tools/call"},
                             {"params", body},
                         };
                         auto response = m_server->HandleMessage(fakeRequest, "http-tool");
                         if (response.has_value() && (*response).contains("result"))
                             res.set_content(DumpJson((*response)["result"]), "application/json");
                         else if (response.has_value() && (*response).contains("error"))
                         {
                             res.status = 400;
                             res.set_content(DumpJson((*response)["error"]), "application/json");
                         }
                         else
                             res.set_content("{}", "application/json"); });
    }

    void HttpTransport::Start()
    {
        // Loopback-only invariant for the OAuth shim: refuse to start if a host has both
        // enabled the shim AND configured a non-loopback bind. Without this, the shim's
        // static credentials would form a remotely-reachable phantom auth surface.
        if (m_config.enableLocalOauthShim && !IsLoopbackBind(m_config.bindAddress))
        {
            Log(LogLevel::Error,
                "[MCP] HttpTransport refusing to start: enableLocalOauthShim=true requires a loopback bindAddress (got '" +
                    m_config.bindAddress + "'). Disable the shim or bind to 127.0.0.1.");
            return;
        }

        if (m_running.exchange(true))
            return;

        ConfigureRoutes();
        m_thread = std::thread([this]()
                               {
                                   if (!m_http->listen(m_config.bindAddress.c_str(), m_config.port))
                                   {
                                       Log(LogLevel::Warn, "[MCP] HttpTransport failed to bind to " +
                                                               m_config.bindAddress + ":" + std::to_string(m_config.port));
                                       m_running = false;
                                   } });

        m_http->wait_until_ready();
        if (m_running)
        {
            Log(LogLevel::Info, "[MCP] HttpTransport listening on http://" + m_config.bindAddress + ":" +
                                    std::to_string(m_config.port) +
                                    (m_config.enableLocalOauthShim ? " (loopback OAuth shim enabled)" : ""));
        }
    }

    void HttpTransport::Stop()
    {
        if (!m_running.exchange(false))
            return;

        Log(LogLevel::Info, "[MCP] HttpTransport stopping");
        if (m_http)
            m_http->stop();
        if (m_thread.joinable())
            m_thread.join();
        Log(LogLevel::Info, "[MCP] HttpTransport stopped");
    }
} // namespace pmcp
