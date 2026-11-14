/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "Network.h"
#include "../world/Entity.h"
#include "../world/components/NetworkSync.h"
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
//======================================

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#elif !defined(__EMSCRIPTEN__)
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
#if defined(__EMSCRIPTEN__)
    // a browser tab has no udp sockets
    void Network::Shutdown() {}
    void Network::Tick() {}
    void Network::PostTick() {}
    bool Network::Host(uint16_t, uint32_t) { SP_LOG_WARNING("Networking isn't available in the browser"); return false; }
    bool Network::Connect(const string&, uint16_t) { SP_LOG_WARNING("Networking isn't available in the browser"); return false; }
    void Network::Disconnect() {}
    bool Network::IsServer() { return false; }
    bool Network::IsClient() { return false; }
    bool Network::IsConnected() { return false; }
    int Network::GetLocalId() { return -1; }
    vector<int> Network::GetPeers() { return {}; }
    float Network::GetPingMs(int) { return 0.0f; }
    bool Network::Send(int, const string&, bool) { return false; }
    bool Network::Broadcast(const string&, bool, int) { return false; }
    vector<NetworkEvent> Network::Poll() { return {}; }
    void Network::Register(NetworkSync*) {}
    void Network::Unregister(NetworkSync*) {}
#else
    namespace
    {
    #ifdef _WIN32
        using socket_t = SOCKET;
        constexpr socket_t invalid_socket = INVALID_SOCKET;
    #else
        using socket_t = int;
        constexpr socket_t invalid_socket = -1;
    #endif

        constexpr uint32_t protocol_id           = 0x53504E31; // "SPN1", packets of other programs or versions are dropped
        constexpr size_t max_packet_size         = 1200;       // stays under a typical mtu, so nothing fragments
        constexpr size_t max_message_size        = 1024;
        constexpr size_t max_unacked             = 1024;
        constexpr size_t max_early               = 256;
        constexpr double connect_retry_sec       = 0.25;
        constexpr double connect_timeout_sec     = 5.0;
        constexpr double timeout_sec             = 5.0;
        constexpr double ping_interval_sec       = 0.5;
        constexpr double resend_min_sec          = 0.1;
        constexpr double send_interval_sec       = 1.0 / 20.0;
        constexpr double interpolation_delay_sec = 0.1;        // two snapshots behind, so there is always a pair to blend

        enum class packet_type : uint8_t
        {
            connect_request = 1,
            connect_accept,
            connect_deny,
            disconnect,
            ping,
            pong,
            reliable,
            ack,
            unreliable,
            snapshot
        };

        enum class session_mode
        {
            none,
            server,
            client
        };

        struct pending_message
        {
            uint32_t sequence = 0;
            string data;
            double last_sent  = 0.0;
        };

        struct peer
        {
            int id                         = -1;
            sockaddr_in address            = {};
            double last_receive            = 0.0;
            double last_ping               = 0.0;
            float rtt_ms                   = 0.0f;
            uint32_t next_send_sequence    = 0;
            uint32_t next_receive_sequence = 0;
            deque<pending_message> unacked;
            map<uint32_t, string> early;
            uint32_t snapshot_sequence     = 0;
            bool has_snapshot              = false;
        };

        // snapshot entry: network id, position, rotation
        constexpr size_t snapshot_entry_size = sizeof(uint32_t) + sizeof(float) * 7;

        socket_t socket_handle      = invalid_socket;
        session_mode mode           = session_mode::none;
        bool client_connected       = false;
        int local_id                = -1;
        uint32_t client_limit       = 8;
        int next_client_id          = 1;
        vector<peer> peers;         // the server's clients, or the client's server
        sockaddr_in server_address  = {};
        double connect_started      = 0.0;
        double last_connect_request = 0.0;
        double last_snapshot_send   = 0.0;
        uint32_t snapshot_sequence  = 0;
        vector<NetworkEvent> events;
    #ifdef _WIN32
        bool winsock_ready = false;
    #endif

        mutex sync_mutex;
        unordered_set<NetworkSync*> syncs;

        double now()
        {
            static const auto start = chrono::steady_clock::now();
            return chrono::duration<double>(chrono::steady_clock::now() - start).count();
        }

        struct packet_writer
        {
            uint8_t data[max_packet_size] = {};
            size_t size                   = 0;

            explicit packet_writer(packet_type type)
            {
                write(protocol_id);
                write(static_cast<uint8_t>(type));
            }

            template <typename T>
            bool write(const T& value)
            {
                return write_bytes(&value, sizeof(T));
            }

            bool write_bytes(const void* bytes, size_t count)
            {
                if (size + count > max_packet_size)
                {
                    return false;
                }
                memcpy(data + size, bytes, count);
                size += count;
                return true;
            }
        };

        struct packet_reader
        {
            const uint8_t* data = nullptr;
            size_t size         = 0;
            size_t offset       = 0;

            template <typename T>
            bool read(T& value)
            {
                return read_bytes(&value, sizeof(T));
            }

            bool read_bytes(void* bytes, size_t count)
            {
                if (offset + count > size)
                {
                    return false;
                }
                memcpy(bytes, data + offset, count);
                offset += count;
                return true;
            }
        };

        bool same_address(const sockaddr_in& a, const sockaddr_in& b)
        {
            return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
        }

        void send_raw(const sockaddr_in& address, const packet_writer& packet)
        {
            if (socket_handle == invalid_socket)
            {
                return;
            }
            sendto(socket_handle, reinterpret_cast<const char*>(packet.data), static_cast<int>(packet.size), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        }

        void send_simple(const sockaddr_in& address, packet_type type)
        {
            packet_writer packet(type);
            send_raw(address, packet);
        }

        void close_socket()
        {
            if (socket_handle == invalid_socket)
            {
                return;
            }
        #ifdef _WIN32
            closesocket(socket_handle);
        #else
            close(socket_handle);
        #endif
            socket_handle = invalid_socket;
        }

        bool open_socket(uint16_t port)
        {
        #ifdef _WIN32
            if (!winsock_ready)
            {
                WSADATA data = {};
                if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                {
                    SP_LOG_ERROR("Network: failed to initialize Winsock");
                    return false;
                }
                winsock_ready = true;
            }
        #endif

            socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket_handle == invalid_socket)
            {
                SP_LOG_ERROR("Network: failed to create a UDP socket");
                return false;
            }

            sockaddr_in local     = {};
            local.sin_family      = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port        = htons(port);
            if (::bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
            {
                SP_LOG_ERROR("Network: failed to bind UDP port %u, it may be in use", port);
                close_socket();
                return false;
            }

        #ifdef _WIN32
            u_long non_blocking = 1;
            ioctlsocket(socket_handle, FIONBIO, &non_blocking);
            // without this a packet to a closed port makes every later recvfrom fail with WSAECONNRESET
            BOOL report_reset = FALSE;
            DWORD bytes       = 0;
            WSAIoctl(socket_handle, SIO_UDP_CONNRESET, &report_reset, sizeof(report_reset), nullptr, 0, &bytes, nullptr, nullptr);
        #else
            fcntl(socket_handle, F_SETFL, fcntl(socket_handle, F_GETFL, 0) | O_NONBLOCK);
        #endif

            return true;
        }

        void clear_sync_states()
        {
            lock_guard<mutex> lock(sync_mutex);
            for (NetworkSync* sync : syncs)
            {
                sync->ClearStates();
            }
        }

        void close_session()
        {
            close_socket();
            mode             = session_mode::none;
            client_connected = false;
            local_id         = -1;
            next_client_id   = 1;
            peers.clear();
            clear_sync_states();
        }

        peer* find_peer_by_address(const sockaddr_in& address)
        {
            for (peer& candidate : peers)
            {
                if (same_address(candidate.address, address))
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        peer* find_peer_by_id(int id)
        {
            for (peer& candidate : peers)
            {
                if (candidate.id == id)
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        void push_event(NetworkEventType type, int peer_id, string data = {})
        {
            events.push_back({ type, peer_id, move(data) });
        }

        void send_message(peer& target, const string& message, bool reliable)
        {
            if (reliable)
            {
                pending_message pending;
                pending.sequence  = target.next_send_sequence++;
                pending.data      = message;
                pending.last_sent = now();

                packet_writer packet(packet_type::reliable);
                packet.write(pending.sequence);
                packet.write(static_cast<uint16_t>(message.size()));
                packet.write_bytes(message.data(), message.size());
                send_raw(target.address, packet);

                target.unacked.push_back(move(pending));
                return;
            }

            packet_writer packet(packet_type::unreliable);
            packet.write(static_cast<uint16_t>(message.size()));
            packet.write_bytes(message.data(), message.size());
            send_raw(target.address, packet);
        }

        void remove_peer(int id, const char* reason)
        {
            for (size_t i = 0; i < peers.size(); i++)
            {
                if (peers[i].id == id)
                {
                    SP_LOG_INFO("Network: client %d disconnected (%s)", id, reason);
                    peers.erase(peers.begin() + i);
                    push_event(NetworkEventType::Disconnected, id, reason);
                    return;
                }
            }
        }

        void lose_server(const char* reason)
        {
            SP_LOG_INFO("Network: disconnected from the server (%s)", reason);
            push_event(NetworkEventType::Disconnected, Network::server_peer, reason);
            close_session();
        }

        unordered_map<uint32_t, NetworkSync*> build_sync_lookup()
        {
            unordered_map<uint32_t, NetworkSync*> lookup;
            lock_guard<mutex> lock(sync_mutex);
            for (NetworkSync* sync : syncs)
            {
                // an inactive template must not take the id of the copy it spawned
                if (sync->IsActiveInHierarchy())
                {
                    lookup[sync->GetNetworkId()] = sync;
                }
            }
            return lookup;
        }

        void handle_reliable(peer& source, packet_reader& reader)
        {
            uint32_t sequence = 0;
            uint16_t length   = 0;
            if (!reader.read(sequence) || !reader.read(length) || length > max_message_size)
            {
                return;
            }
            string message(length, '\0');
            if (!reader.read_bytes(message.data(), length))
            {
                return;
            }

            // always ack, the previous ack may have been lost
            packet_writer ack(packet_type::ack);
            ack.write(sequence);
            send_raw(source.address, ack);

            const int32_t distance = static_cast<int32_t>(sequence - source.next_receive_sequence);
            if (distance < 0)
            {
                return; // a duplicate
            }
            if (distance > 0)
            {
                if (source.early.size() < max_early)
                {
                    source.early.emplace(sequence, move(message));
                }
                return;
            }

            push_event(NetworkEventType::Message, source.id, move(message));
            source.next_receive_sequence++;
            for (auto it = source.early.find(source.next_receive_sequence); it != source.early.end(); it = source.early.find(source.next_receive_sequence))
            {
                push_event(NetworkEventType::Message, source.id, move(it->second));
                source.early.erase(it);
                source.next_receive_sequence++;
            }
        }

        void handle_snapshot(peer& source, packet_reader& reader, unordered_map<uint32_t, NetworkSync*>& lookup, bool& lookup_built)
        {
            uint32_t sequence = 0;
            uint16_t count    = 0;
            if (!reader.read(sequence) || !reader.read(count))
            {
                return;
            }

            // snapshots are unreliable, an older one arriving late would move things backwards
            if (source.has_snapshot && static_cast<int32_t>(sequence - source.snapshot_sequence) <= 0)
            {
                return;
            }
            source.has_snapshot      = true;
            source.snapshot_sequence = sequence;

            if (!lookup_built)
            {
                lookup       = build_sync_lookup();
                lookup_built = true;
            }

            const double time = now();
            for (uint16_t i = 0; i < count; i++)
            {
                uint32_t id = 0;
                float values[7] = {};
                if (!reader.read(id) || !reader.read_bytes(values, sizeof(values)))
                {
                    return;
                }

                auto it = lookup.find(id);
                if (it == lookup.end())
                {
                    continue;
                }

                NetworkSync* sync = it->second;
                if (sync->IsLocallyOwned())
                {
                    continue;
                }
                // a client may only move what it owns
                if (mode == session_mode::server && sync->GetOwner() != source.id)
                {
                    continue;
                }

                sync->PushState(time, Vector3(values[0], values[1], values[2]), Quaternion(values[3], values[4], values[5], values[6]));
            }
        }

        void handle_packet(const sockaddr_in& from, const uint8_t* data, size_t size, unordered_map<uint32_t, NetworkSync*>& lookup, bool& lookup_built)
        {
            packet_reader reader;
            reader.data = data;
            reader.size = size;

            uint32_t id      = 0;
            uint8_t type_raw = 0;
            if (!reader.read(id) || id != protocol_id || !reader.read(type_raw))
            {
                return;
            }
            const packet_type type = static_cast<packet_type>(type_raw);

            peer* source = nullptr;
            if (mode == session_mode::server)
            {
                source = find_peer_by_address(from);
                if (type == packet_type::connect_request)
                {
                    if (!source)
                    {
                        if (peers.size() >= client_limit)
                        {
                            send_simple(from, packet_type::connect_deny);
                            return;
                        }

                        peer joined;
                        joined.id           = next_client_id++;
                        joined.address      = from;
                        joined.last_receive = now();
                        peers.push_back(joined);
                        source = &peers.back();

                        char address_text[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &from.sin_addr, address_text, sizeof(address_text));
                        SP_LOG_INFO("Network: client %d connected from %s:%u", source->id, address_text, ntohs(from.sin_port));
                        push_event(NetworkEventType::Connected, source->id);
                    }

                    // also answers a retry whose accept got lost
                    packet_writer accept(packet_type::connect_accept);
                    accept.write(static_cast<uint16_t>(source->id));
                    send_raw(from, accept);
                    source->last_receive = now();
                    return;
                }
            }
            else
            {
                if (!same_address(from, server_address))
                {
                    return;
                }

                if (type == packet_type::connect_accept)
                {
                    uint16_t assigned = 0;
                    if (!client_connected && reader.read(assigned))
                    {
                        client_connected = true;
                        local_id         = assigned;
                        peer server;
                        server.id           = Network::server_peer;
                        server.address      = server_address;
                        server.last_receive = now();
                        peers.push_back(server);
                        SP_LOG_INFO("Network: connected to the server as client %d", local_id);
                        push_event(NetworkEventType::Connected, Network::server_peer);
                    }
                    return;
                }

                if (type == packet_type::connect_deny)
                {
                    if (!client_connected)
                    {
                        SP_LOG_WARNING("Network: the server is full");
                        push_event(NetworkEventType::Failed, Network::server_peer, "server full");
                        close_session();
                    }
                    return;
                }

                source = peers.empty() ? nullptr : &peers.front();
            }

            if (!source)
            {
                return;
            }
            source->last_receive = now();

            switch (type)
            {
                case packet_type::disconnect:
                {
                    if (mode == session_mode::server)
                    {
                        remove_peer(source->id, "left");
                    }
                    else
                    {
                        lose_server("server closed");
                    }
                    break;
                }

                case packet_type::ping:
                {
                    double sent = 0.0;
                    if (reader.read(sent))
                    {
                        packet_writer pong(packet_type::pong);
                        pong.write(sent);
                        send_raw(source->address, pong);
                    }
                    break;
                }

                case packet_type::pong:
                {
                    double sent = 0.0;
                    if (reader.read(sent))
                    {
                        const float rtt = static_cast<float>((now() - sent) * 1000.0);
                        source->rtt_ms  = source->rtt_ms <= 0.0f ? rtt : source->rtt_ms * 0.8f + rtt * 0.2f;
                    }
                    break;
                }

                case packet_type::reliable:
                {
                    handle_reliable(*source, reader);
                    break;
                }

                case packet_type::ack:
                {
                    uint32_t sequence = 0;
                    if (reader.read(sequence))
                    {
                        for (auto it = source->unacked.begin(); it != source->unacked.end(); ++it)
                        {
                            if (it->sequence == sequence)
                            {
                                source->unacked.erase(it);
                                break;
                            }
                        }
                    }
                    break;
                }

                case packet_type::unreliable:
                {
                    uint16_t length = 0;
                    if (reader.read(length) && length <= max_message_size)
                    {
                        string message(length, '\0');
                        if (reader.read_bytes(message.data(), length))
                        {
                            push_event(NetworkEventType::Message, source->id, move(message));
                        }
                    }
                    break;
                }

                case packet_type::snapshot:
                {
                    handle_snapshot(*source, reader, lookup, lookup_built);
                    break;
                }

                default:
                    break;
            }
        }

        void receive_packets()
        {
            unordered_map<uint32_t, NetworkSync*> lookup;
            bool lookup_built = false;

            uint8_t buffer[max_packet_size + 1];
            while (mode != session_mode::none && socket_handle != invalid_socket)
            {
                sockaddr_in from = {};
            #ifdef _WIN32
                int from_size = sizeof(from);
            #else
                socklen_t from_size = sizeof(from);
            #endif
                const int received = recvfrom(socket_handle, reinterpret_cast<char*>(buffer), sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &from_size);
                if (received <= 0)
                {
                    break;
                }
                if (static_cast<size_t>(received) > max_packet_size)
                {
                    continue;
                }
                handle_packet(from, buffer, static_cast<size_t>(received), lookup, lookup_built);
            }
        }

        void service_peers()
        {
            const double time = now();

            if (mode == session_mode::client && !client_connected)
            {
                if (time - connect_started > connect_timeout_sec)
                {
                    SP_LOG_WARNING("Network: no answer from the server");
                    push_event(NetworkEventType::Failed, Network::server_peer, "no answer");
                    close_session();
                    return;
                }
                if (time - last_connect_request >= connect_retry_sec)
                {
                    send_simple(server_address, packet_type::connect_request);
                    last_connect_request = time;
                }
                return;
            }

            vector<int> timed_out;
            vector<int> overflowed;
            for (peer& target : peers)
            {
                if (time - target.last_receive > timeout_sec)
                {
                    timed_out.push_back(target.id);
                    continue;
                }
                if (target.unacked.size() > max_unacked)
                {
                    overflowed.push_back(target.id);
                    continue;
                }

                if (time - target.last_ping >= ping_interval_sec)
                {
                    packet_writer ping(packet_type::ping);
                    ping.write(time);
                    send_raw(target.address, ping);
                    target.last_ping = time;
                }

                const double resend_after = max(resend_min_sec, target.rtt_ms * 0.0015);
                for (pending_message& pending : target.unacked)
                {
                    if (time - pending.last_sent >= resend_after)
                    {
                        packet_writer packet(packet_type::reliable);
                        packet.write(pending.sequence);
                        packet.write(static_cast<uint16_t>(pending.data.size()));
                        packet.write_bytes(pending.data.data(), pending.data.size());
                        send_raw(target.address, packet);
                        pending.last_sent = time;
                    }
                }
            }

            for (int id : timed_out)
            {
                if (mode == session_mode::server)
                {
                    remove_peer(id, "timed out");
                }
                else
                {
                    lose_server("timed out");
                    return;
                }
            }
            for (int id : overflowed)
            {
                if (mode == session_mode::server)
                {
                    if (peer* target = find_peer_by_id(id))
                    {
                        send_simple(target->address, packet_type::disconnect);
                    }
                    remove_peer(id, "too many unacknowledged messages");
                }
                else
                {
                    send_simple(server_address, packet_type::disconnect);
                    lose_server("too many unacknowledged messages");
                    return;
                }
            }
        }

        void apply_remote_transforms()
        {
            const double render_time = now() - interpolation_delay_sec;
            lock_guard<mutex> lock(sync_mutex);
            for (NetworkSync* sync : syncs)
            {
                if (sync->IsLocallyOwned() || !sync->IsActiveInHierarchy())
                {
                    continue;
                }

                Vector3 position;
                Quaternion rotation;
                if (sync->SampleState(render_time, position, rotation))
                {
                    Entity* entity = sync->GetEntity();
                    entity->SetPosition(position);
                    entity->SetRotation(rotation);
                }
            }
        }

        struct snapshot_entry
        {
            uint32_t id = 0;
            int owner   = 0;
            Vector3 position;
            Quaternion rotation;
        };

        void send_snapshot(peer& target, const vector<snapshot_entry>& entries)
        {
            constexpr size_t header_size     = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t);
            constexpr size_t entries_per_packet = (max_packet_size - header_size) / snapshot_entry_size;

            vector<const snapshot_entry*> visible;
            for (const snapshot_entry& entry : entries)
            {
                // a client already has its own entities
                if (mode == session_mode::client || entry.owner != target.id)
                {
                    visible.push_back(&entry);
                }
            }

            for (size_t first = 0; first < visible.size(); first += entries_per_packet)
            {
                const size_t count = min(entries_per_packet, visible.size() - first);
                packet_writer packet(packet_type::snapshot);
                packet.write(snapshot_sequence++);
                packet.write(static_cast<uint16_t>(count));
                for (size_t i = first; i < first + count; i++)
                {
                    const snapshot_entry& entry = *visible[i];
                    const float values[7] =
                    {
                        entry.position.x, entry.position.y, entry.position.z,
                        entry.rotation.x, entry.rotation.y, entry.rotation.z, entry.rotation.w
                    };
                    packet.write(entry.id);
                    packet.write_bytes(values, sizeof(values));
                }
                send_raw(target.address, packet);
            }
        }

        void send_snapshots()
        {
            vector<snapshot_entry> entries;
            {
                lock_guard<mutex> lock(sync_mutex);
                for (NetworkSync* sync : syncs)
                {
                    if (!sync->IsActiveInHierarchy())
                    {
                        continue;
                    }

                    snapshot_entry entry;
                    entry.id    = sync->GetNetworkId();
                    entry.owner = sync->GetOwner();
                    if (sync->IsLocallyOwned())
                    {
                        entry.position = sync->GetEntity()->GetPosition();
                        entry.rotation = sync->GetEntity()->GetRotation();
                    }
                    // the server relays what clients sent, from the newest state rather than its delayed copy
                    else if (mode != session_mode::server || !sync->GetLatestState(entry.position, entry.rotation))
                    {
                        continue;
                    }
                    entries.push_back(entry);
                }
            }

            if (entries.empty())
            {
                return;
            }

            for (peer& target : peers)
            {
                send_snapshot(target, entries);
            }
        }

        bool is_session_live()
        {
            return mode == session_mode::server || (mode == session_mode::client && client_connected);
        }
    }

    void Network::Shutdown()
    {
        Disconnect();
    #ifdef _WIN32
        if (winsock_ready)
        {
            WSACleanup();
            winsock_ready = false;
        }
    #endif
    }

    void Network::Tick()
    {
        if (mode == session_mode::none)
        {
            return;
        }

        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            SP_LOG_INFO("Network: play stopped, closing the session");
            Disconnect();
            return;
        }

        receive_packets();
        if (mode == session_mode::none)
        {
            return;
        }

        service_peers();
        if (is_session_live())
        {
            apply_remote_transforms();
        }
    }

    void Network::PostTick()
    {
        if (!is_session_live())
        {
            return;
        }

        const double time = now();
        if (time - last_snapshot_send < send_interval_sec)
        {
            return;
        }
        last_snapshot_send = time;
        send_snapshots();
    }

    bool Network::Host(uint16_t port, uint32_t max_clients)
    {
        Disconnect();
        events.clear();

        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            SP_LOG_WARNING("Network: a session can only be hosted while playing");
            return false;
        }

        if (!open_socket(port))
        {
            return false;
        }

        mode           = session_mode::server;
        local_id       = server_peer;
        client_limit   = max(1u, max_clients);
        next_client_id = 1;
        SP_LOG_INFO("Network: hosting on UDP port %u for up to %u clients", port, client_limit);
        return true;
    }

    bool Network::Connect(const string& address, uint16_t port)
    {
        Disconnect();
        events.clear();

        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            SP_LOG_WARNING("Network: connecting is only possible while playing");
            return false;
        }

        if (!open_socket(0))
        {
            return false;
        }

        addrinfo hints     = {};
        hints.ai_family    = AF_INET;
        hints.ai_socktype  = SOCK_DGRAM;
        addrinfo* resolved = nullptr;
        if (getaddrinfo(address.c_str(), nullptr, &hints, &resolved) != 0 || !resolved)
        {
            SP_LOG_ERROR("Network: can't resolve \"%s\"", address.c_str());
            close_socket();
            return false;
        }
        server_address          = *reinterpret_cast<sockaddr_in*>(resolved->ai_addr);
        server_address.sin_port = htons(port);
        freeaddrinfo(resolved);

        mode                 = session_mode::client;
        client_connected     = false;
        local_id             = -1;
        connect_started      = now();
        last_connect_request = connect_started;
        send_simple(server_address, packet_type::connect_request);
        SP_LOG_INFO("Network: connecting to %s:%u", address.c_str(), port);
        return true;
    }

    void Network::Disconnect()
    {
        if (mode == session_mode::none)
        {
            return;
        }

        // a few copies, one of them should get through
        for (int i = 0; i < 3; i++)
        {
            if (mode == session_mode::server)
            {
                for (const peer& target : peers)
                {
                    send_simple(target.address, packet_type::disconnect);
                }
            }
            else
            {
                send_simple(server_address, packet_type::disconnect);
            }
        }

        SP_LOG_INFO("Network: session closed");
        close_session();
    }

    bool Network::IsServer()
    {
        return mode == session_mode::server;
    }

    bool Network::IsClient()
    {
        return mode == session_mode::client;
    }

    bool Network::IsConnected()
    {
        return is_session_live();
    }

    int Network::GetLocalId()
    {
        return is_session_live() ? local_id : -1;
    }

    vector<int> Network::GetPeers()
    {
        vector<int> ids;
        for (const peer& target : peers)
        {
            ids.push_back(target.id);
        }
        return ids;
    }

    float Network::GetPingMs(int peer_id)
    {
        const peer* target = find_peer_by_id(peer_id);
        return target ? target->rtt_ms : 0.0f;
    }

    bool Network::Send(int peer_id, const string& message, bool reliable)
    {
        if (!is_session_live())
        {
            return false;
        }

        if (message.size() > max_message_size)
        {
            SP_LOG_ERROR("Network: a message is %zu bytes, the limit is %zu", message.size(), max_message_size);
            return false;
        }

        peer* target = find_peer_by_id(peer_id);
        if (!target)
        {
            return false;
        }

        send_message(*target, message, reliable);
        return true;
    }

    bool Network::Broadcast(const string& message, bool reliable, int except)
    {
        if (!is_session_live())
        {
            return false;
        }

        if (message.size() > max_message_size)
        {
            SP_LOG_ERROR("Network: a message is %zu bytes, the limit is %zu", message.size(), max_message_size);
            return false;
        }

        for (peer& target : peers)
        {
            if (target.id != except)
            {
                send_message(target, message, reliable);
            }
        }
        return true;
    }

    vector<NetworkEvent> Network::Poll()
    {
        vector<NetworkEvent> polled;
        polled.swap(events);
        return polled;
    }

    void Network::Register(NetworkSync* sync)
    {
        lock_guard<mutex> lock(sync_mutex);
        syncs.insert(sync);
    }

    void Network::Unregister(NetworkSync* sync)
    {
        lock_guard<mutex> lock(sync_mutex);
        syncs.erase(sync);
    }
#endif
}
