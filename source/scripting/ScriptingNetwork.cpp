/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include <sol/sol.hpp>
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../network/Network.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting
{
    namespace
    {
        const char* event_type_name(NetworkEventType type)
        {
            switch (type)
            {
                case NetworkEventType::Connected:    return "connected";
                case NetworkEventType::Disconnected: return "disconnected";
                case NetworkEventType::Failed:       return "failed";
                default:                             return "message";
            }
        }

        bool valid_port(int port)
        {
            if (port < 1 || port > 65535)
            {
                SP_LOG_ERROR("Network: %d is not a valid port", port);
                return false;
            }
            return true;
        }
    }

    void register_network(sol::state_view state)
    {
        visual::table table(state, "Network");

        table["Host"] = [](int port, sol::optional<int> max_clients) -> bool
        {
            return valid_port(port) && Network::Host(static_cast<uint16_t>(port), static_cast<uint32_t>(max(1, max_clients.value_or(8))));
        };

        table["Connect"] = [](const string& address, int port) -> bool
        {
            return valid_port(port) && Network::Connect(address, static_cast<uint16_t>(port));
        };

        table["Disconnect"]  = &Network::Disconnect;
        table["IsServer"]    = &Network::IsServer;
        table["IsClient"]    = &Network::IsClient;
        table["IsConnected"] = &Network::IsConnected;
        table["GetLocalId"]  = &Network::GetLocalId;
        table["GetPingMs"]   = &Network::GetPingMs;

        table["GetPeers"] = [](sol::this_state lua_state) -> sol::table
        {
            sol::state_view lua(lua_state);
            const vector<int> peers = Network::GetPeers();
            sol::table result       = lua.create_table(static_cast<int>(peers.size()), 0);
            for (size_t i = 0; i < peers.size(); i++)
            {
                result[i + 1] = peers[i];
            }
            return result;
        };

        // reliable by default: delivered once and in order, unreliable messages may be lost or reordered
        table["Send"] = [](int peer, const string& message, sol::optional<bool> reliable) -> bool
        {
            return Network::Send(peer, message, reliable.value_or(true));
        };

        table["Broadcast"] = [](const string& message, sol::optional<bool> reliable, sol::optional<int> except) -> bool
        {
            return Network::Broadcast(message, reliable.value_or(true), except.value_or(-1));
        };

        // { type = "connected" | "disconnected" | "failed" | "message", peer = id, data = string }, oldest first
        table["Poll"] = [](sol::this_state lua_state) -> sol::table
        {
            sol::state_view lua(lua_state);
            const vector<NetworkEvent> events = Network::Poll();
            sol::table result                 = lua.create_table(static_cast<int>(events.size()), 0);
            for (size_t i = 0; i < events.size(); i++)
            {
                sol::table event = lua.create_table(0, 3);
                event["type"]    = event_type_name(events[i].type);
                event["peer"]    = events[i].peer;
                event["data"]    = events[i].data;
                result[i + 1]    = event;
            }
            return result;
        };

        table["ServerPeer"] = Network::server_peer;
    }
}
