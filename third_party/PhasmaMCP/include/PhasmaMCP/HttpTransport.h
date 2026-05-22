#pragma once

#include "PhasmaMCP/LogLevel.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace httplib
{
    class Server;
}

namespace pmcp
{
    class Server;

    struct HttpTransportConfig
    {
        std::string bindAddress = "127.0.0.1";
        int port = 8765;

        // Disabled by default. When true, the transport publishes OAuth 2.0 metadata,
        // dynamic-registration, authorize, and token endpoints expected by some MCP clients
        // (e.g. Claude Code) that refuse to attach to HTTP MCP servers without them.
        //
        // The shim does NOT implement real authentication. The token returned by `/oauth/token`
        // is static, non-secret, and exists only to satisfy clients that probe for OAuth metadata.
        // Real safety comes from two structural invariants enforced by this transport:
        //   1. Start() refuses to bind to a non-loopback address while the shim is enabled —
        //      so the phantom auth surface can never be reached from off-host.
        //   2. /mcp and /tool reject requests carrying a non-local `Origin` header (browser-CSRF
        //      defense for the localhost-attack-from-malicious-page scenario).
        //
        // Hosts that bind beyond loopback must keep the shim OFF and put the transport behind
        // their own auth (reverse proxy, mTLS, etc.).
        bool enableLocalOauthShim = false;

        // Token returned by `/oauth/token` when the shim is enabled. Non-secret on purpose;
        // this is just a value some clients require for spec compliance. Override per host
        // for cosmetic reasons or to mark the deployment in client logs.
        std::string staticBearerToken = "phasma-mcp-local-static-token";

        LogCallback log;
    };

    // httplib-backed MCP transport. Owns a thread + httplib::Server bound to bindAddress:port.
    // All MCP request bodies are forwarded to `pmcp::Server::HandleMessage`; the transport
    // is responsible for HTTP framing, the `MCP-Protocol-Version` header, Origin checks, and
    // loopback-only enforcement when the OAuth compatibility shim is enabled.
    class HttpTransport
    {
    public:
        HttpTransport(Server *server, HttpTransportConfig config);
        ~HttpTransport();

        HttpTransport(const HttpTransport &) = delete;
        HttpTransport &operator=(const HttpTransport &) = delete;

        // Starts the listen thread. If `enableLocalOauthShim=true` and `bindAddress` is not a
        // loopback address (127.0.0.0/8 or ::1), Start() logs an error and refuses to start —
        // the shim's static credentials must never be reachable beyond the local host.
        void Start();
        void Stop();

        bool IsRunning() const { return m_running.load(); }
        int Port() const { return m_config.port; }

    private:
        void ConfigureRoutes();
        void Log(LogLevel level, const std::string &message) const;

        Server *m_server = nullptr;
        HttpTransportConfig m_config;
        std::unique_ptr<httplib::Server> m_http;
        std::thread m_thread;
        std::atomic<bool> m_running{false};
    };
} // namespace pmcp
