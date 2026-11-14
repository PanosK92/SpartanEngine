/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====
#include <cstdint>
#include <string>
#include <vector>
//================

namespace spartan
{
    class NetworkSync;

    enum class NetworkEventType : uint8_t
    {
        Connected,    // a client joined (server), or the server accepted us (client)
        Disconnected, // a peer left or timed out, data holds the reason
        Failed,       // connecting gave up, data holds the reason
        Message       // a message from a peer, data holds it
    };

    struct NetworkEvent
    {
        NetworkEventType type = NetworkEventType::Message;
        int peer              = -1;
        std::string data;
    };

    // UDP sessions between one server and its clients: reliable ordered and unreliable messages,
    // and transform replication for entities with a NetworkSync component. The server is peer 0,
    // clients get ids from 1. Sessions only live while the engine is playing.
    class Network
    {
    public:
        static constexpr int server_peer = 0;

        static void Shutdown();
        // receives packets and applies replicated transforms, before the world ticks
        static void Tick();
        // sends replication snapshots, after the world ticked
        static void PostTick();

        static bool Host(uint16_t port, uint32_t max_clients = 8);
        static bool Connect(const std::string& address, uint16_t port);
        static void Disconnect();

        static bool IsServer();
        static bool IsClient();
        static bool IsConnected();
        static int GetLocalId();
        static std::vector<int> GetPeers();
        static float GetPingMs(int peer);

        static bool Send(int peer, const std::string& message, bool reliable = true);
        static bool Broadcast(const std::string& message, bool reliable = true, int except = -1);
        // the events since the last call, oldest first
        static std::vector<NetworkEvent> Poll();

        // NetworkSync components register themselves for replication
        static void Register(NetworkSync* sync);
        static void Unregister(NetworkSync* sync);
    };
}
