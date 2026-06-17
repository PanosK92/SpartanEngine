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

//= INCLUDES =========
#include "pch.h"
#include "McpServer.h"
#include "McpQueue.h"
#include <cctype>
#include <cstdlib>
#include <thread>
//====================

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace spartan
{
    namespace
    {
    #ifdef _WIN32
        using socket_t = SOCKET;
        constexpr socket_t invalid_socket = INVALID_SOCKET;
    #else
        using socket_t = int;
        constexpr socket_t invalid_socket = -1;
    #endif

        std::atomic<bool> is_running = false;
        std::thread server_thread;
        socket_t listen_socket = invalid_socket;
        socket_t active_client_socket = invalid_socket;
        std::mutex client_mutex;
        uint16_t mcp_port = 47777;
    #ifdef _WIN32
        bool winsock_initialized = false;
    #endif

        void close_socket(socket_t socket)
        {
            if (socket == invalid_socket)
            {
                return;
            }

        #ifdef _WIN32
            closesocket(socket);
        #else
            close(socket);
        #endif
        }

        void shutdown_socket(socket_t socket)
        {
            if (socket == invalid_socket)
            {
                return;
            }

        #ifdef _WIN32
            shutdown(socket, SD_BOTH);
        #else
            shutdown(socket, SHUT_RDWR);
        #endif
        }

        bool has_argument(const std::vector<std::string>& args, const std::string& argument)
        {
            for (const std::string& arg : args)
            {
                if (arg == argument)
                {
                    return true;
                }
            }

            return false;
        }

        uint16_t parse_port(const std::vector<std::string>& args)
        {
            constexpr const char* prefix = "--mcp-port=";
            for (const std::string& arg : args)
            {
                if (arg.rfind(prefix, 0) == 0)
                {
                    const unsigned long value = std::strtoul(arg.c_str() + std::strlen(prefix), nullptr, 10);
                    if (value > 0 && value <= std::numeric_limits<uint16_t>::max())
                    {
                        return static_cast<uint16_t>(value);
                    }
                }
            }

            return mcp_port;
        }

        int hex_to_int(char value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }

            return -1;
        }

        std::string url_decode(const std::string& value)
        {
            std::string decoded;
            decoded.reserve(value.size());

            for (size_t i = 0; i < value.size(); i++)
            {
                if (value[i] == '%' && i + 2 < value.size())
                {
                    const int high = hex_to_int(value[i + 1]);
                    const int low  = hex_to_int(value[i + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }

                decoded.push_back(value[i]);
            }

            return decoded;
        }

        McpRequest parse_request(const std::string& line)
        {
            McpRequest request;

            size_t token_begin = 0;
            while (token_begin < line.size())
            {
                const size_t token_end = line.find(' ', token_begin);
                const std::string token = line.substr(token_begin, token_end == std::string::npos ? std::string::npos : token_end - token_begin);

                if (!token.empty())
                {
                    if (request.command.empty())
                    {
                        request.command = token;
                    }
                    else
                    {
                        const size_t separator = token.find('=');
                        if (separator != std::string::npos)
                        {
                            const std::string key   = token.substr(0, separator);
                            const std::string value = token.substr(separator + 1);
                            request.arguments[key]  = url_decode(value);
                        }
                    }
                }

                if (token_end == std::string::npos)
                {
                    break;
                }
                token_begin = token_end + 1;
            }

            return request;
        }

        bool send_all(socket_t socket, const std::string& data)
        {
            size_t bytes_sent = 0;
            while (bytes_sent < data.size())
            {
                const int sent = send(socket, data.data() + bytes_sent, static_cast<int>(data.size() - bytes_sent), 0);
                if (sent <= 0)
                {
                    return false;
                }

                bytes_sent += static_cast<size_t>(sent);
            }

            return true;
        }

        void handle_client(socket_t client_socket)
        {
            std::string buffer;
            char receive_buffer[4096];

            while (is_running)
            {
                const int received = recv(client_socket, receive_buffer, sizeof(receive_buffer), 0);
                if (received <= 0)
                {
                    break;
                }

                buffer.append(receive_buffer, received);

                size_t newline = buffer.find('\n');
                while (newline != std::string::npos)
                {
                    std::string line = buffer.substr(0, newline);
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    buffer.erase(0, newline + 1);

                    if (!line.empty())
                    {
                        McpRequest request = parse_request(line);
                        std::string response       = McpQueue::Submit(request);
                        response.push_back('\n');

                        if (!send_all(client_socket, response))
                        {
                            return;
                        }
                    }

                    newline = buffer.find('\n');
                }

                if (buffer.size() > 64 * 1024)
                {
                    send_all(client_socket, "{\"ok\":false,\"error\":\"request is too large\"}\n");
                    break;
                }
            }
        }

        void server_loop()
        {
            while (is_running)
            {
                sockaddr_in client_address = {};
            #ifdef _WIN32
                int client_address_size = sizeof(client_address);
            #else
                socklen_t client_address_size = sizeof(client_address);
            #endif

                const socket_t client_socket = accept(listen_socket, reinterpret_cast<sockaddr*>(&client_address), &client_address_size);
                if (client_socket == invalid_socket)
                {
                    if (is_running)
                    {
                        SP_LOG_WARNING("MCP accept failed");
                    }
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(client_mutex);
                    active_client_socket = client_socket;
                }

                handle_client(client_socket);

                {
                    std::lock_guard<std::mutex> lock(client_mutex);
                    if (active_client_socket == client_socket)
                    {
                        active_client_socket = invalid_socket;
                    }
                }

                shutdown_socket(client_socket);
                close_socket(client_socket);
            }
        }
    }

    void McpServer::Initialize(const std::vector<std::string>& args)
    {
        if (!has_argument(args, "--mcp-control"))
        {
            return;
        }

        Start(parse_port(args));
    }

    void McpServer::Start(uint16_t port)
    {
        if (is_running)
        {
            return;
        }

        mcp_port = port;
        McpQueue::Initialize();

    #ifdef _WIN32
        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            SP_LOG_ERROR("Failed to initialize Winsock for MCP");
            return;
        }
        winsock_initialized = true;
    #endif

        listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == invalid_socket)
        {
            SP_LOG_ERROR("Failed to create MCP socket");
            Shutdown();
            return;
        }

        int reuse_address = 1;
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

        sockaddr_in address = {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(mcp_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            SP_LOG_ERROR("Failed to bind MCP to 127.0.0.1:%u", mcp_port);
            Shutdown();
            return;
        }

        if (listen(listen_socket, 1) != 0)
        {
            SP_LOG_ERROR("Failed to listen for MCP on 127.0.0.1:%u", mcp_port);
            Shutdown();
            return;
        }

        is_running = true;
        server_thread = std::thread(server_loop);
        SP_LOG_INFO("MCP is listening on 127.0.0.1:%u", mcp_port);
    }

    void McpServer::Shutdown()
    {
        McpQueue::Shutdown();
        is_running = false;

        shutdown_socket(listen_socket);
        close_socket(listen_socket);
        listen_socket = invalid_socket;

        {
            std::lock_guard<std::mutex> lock(client_mutex);
            shutdown_socket(active_client_socket);
        }

        if (server_thread.joinable())
        {
            server_thread.join();
        }

    #ifdef _WIN32
        if (winsock_initialized)
        {
            WSACleanup();
            winsock_initialized = false;
        }
    #endif
    }

    void McpServer::Tick()
    {
        if (is_running)
        {
            McpQueue::Tick();
        }
    }

    bool McpServer::IsRunning()
    {
        return is_running;
    }

    uint16_t McpServer::GetPort()
    {
        return mcp_port;
    }
}
