# Networking

A client/server layer over plain UDP sockets (Winsock on Windows, BSD sockets elsewhere), with
no third-party dependency. One server talks to its clients; clients never talk to each other.
It is meant for LAN and same-machine sessions. There is no lobby, NAT traversal, encryption,
client-side prediction or physics replication.

## Sessions

`Network::Host(port, max_clients)` binds a UDP port and accepts clients. `Network::Connect(address,
port)` resolves the address and repeats a connect request every 0.25 s until the server answers
or 5 s pass. The server is peer 0 (`Network::server_peer`). Clients get increasing ids from 1, which
are never reused within a session. A full server answers with a deny. Both calls require play
mode, and stopping play closes the session, sending a disconnect to every peer.

Peers ping each other every 0.5 s. `GetPingMs(peer)` is a smoothed round-trip time. A peer that sends
nothing for 5 s is dropped with the reason `timed out`.

`Poll()` returns the events since the last call, oldest first:

| Event | Server | Client |
| ----- | ------ | ------ |
| `Connected` | a client joined, `peer` is its id | the server accepted us, `GetLocalId()` is our id |
| `Disconnected` | a client left or timed out | the server closed, timed out or dropped us |
| `Failed` | | connecting gave up (`no answer`, `server full`) |
| `Message` | a message from a client | a message from the server |

## Messages

`Send(peer, message, reliable)` and `Broadcast(message, reliable, except)` carry strings of up to
1024 bytes. A client can only send to the server; the game relays to other clients itself.
Reliable messages carry a sequence number, are resent until acknowledged (after
`max(100 ms, 1.5 × rtt)`), and are delivered in order with duplicates dropped. Up to 256 early
arrivals are buffered. A peer with more than 1024 unacknowledged messages is disconnected.
Unreliable messages are sent once.

Every packet starts with the protocol id `SPN1` and is at most 1200 bytes, so it never fragments.
Packets with another id are ignored.

## Replication

A [`NetworkSync`](../world/components/NetworkSync.h) component registers its entity with `Network`. It has:

- **Network id**: identifies the entity on every peer. 0 derives one from the entity id, which
  matches across peers when they loaded the same world. Entities spawned at runtime need an
  explicit id that every peer agrees on.
- **Owner**: the peer that sends this entity's transform, 0 for the server.
- **Interpolate**: receivers blend between snapshots. Off snaps to the newest one.

20 times a second, `PostTick` sends a snapshot of every active entity the local peer owns: network
id, position and rotation. Snapshots are unreliable and split into packets of up to 37 entries. The
server sends its own entities and the latest state of every client-owned entity to all clients,
skipping the owner. It accepts a client's snapshot only for entities that client owns. Each packet
has its own sequence number, and a packet older than the newest one received from that peer is dropped.

`Tick` runs before physics and the world tick. It applies received transforms to entities the local
peer does not own, sampled 100 ms in the past so there are normally two snapshots to blend.
Position is interpolated linearly and rotation with a quaternion lerp. Only the world transform is
replicated: a replicated entity should have no parent, or a parent that does not move.

Outside a session, `GetLocalId()` is -1 and every `NetworkSync` counts as locally owned, so the
same scripts run offline.

## Lua

The `Network` table exposes the functions above, with events returned as
`{ type = "connected" | "disconnected" | "failed" | "message", peer, data }`, and
`ComponentType.NetworkSync` exposes `Get/SetNetworkId`, `Get/SetOwner`, `Get/SetInterpolate` and
`IsLocallyOwned`. See [Lua scripting](../../docs/scripting.md) and
`project/multiplayer_example_resources/scripts/multiplayer_example.lua`.

## Example

[`worlds/multiplayer_example.world`](../../worlds/multiplayer_example.world) is an arena for one
server and its clients. Start two instances and press play in both. In one, press **H** to host
on UDP port 27500; in the other, press **J** to join `127.0.0.1`. Each player gets a coloured
capsule moved with WASD; Space sends a pulse, a reliable message the server relays to the others.
**L** leaves. The orange orb is moved by the server only. To play across machines, change
`server_address` in the script and allow UDP 27500 through the firewall.

The browser build has no sockets: every call fails and `IsConnected()` is false.
