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

#include "pch.h"
#include "CarSurfaceEffects.h"
#include "CarSimulation.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"
#include "../world/components/Render.h"
#include "../world/components/Terrain.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Camera.h"
#include "../rhi/RHI_Vertex.h"
#include "../profiling/Profiler.h"
#include "../core/ThreadPool.h"
#include "../geometry/Mesh.h"
#include "../physics/PhysicsWorld.h"
#include <atomic>
#include <map>
#include "../math/Ray.h"
#include <array>
#include <numeric>

namespace spartan
{
    using namespace math;
    namespace
    {
        struct Surface
        {
            Material* material = nullptr;
            Color color = Color(0.15f, 0.085f, 0.035f, 0.75f);
            float roughness = 0.88f;
            float grass = 0.0f;
            float wet = 0.0f;
            float loose = 1.0f;
            float size = 0.025f;
        };

        Surface sample_surface(const car::wheel& wheel, const Vector3& point)
        {
            Surface s;
            s.loose = 0.0f;
            if (!wheel.contact_actor || !wheel.contact_actor->userData) return s;
            Entity* ground = static_cast<Entity*>(wheel.contact_actor->userData);
            Terrain* terrain = nullptr;
            for (Entity* e = ground; e; e = e->GetParent())
                if ((terrain = e->GetComponent<Terrain>())) break;
            // A road above terrain must never borrow the biome underneath it.
            auto* ground_physics = ground->GetComponent<Physics>();
            if (!terrain && ground_physics && ground_physics->GetBodyType() == BodyType::Heightfield)
            {
                for (Entity* e : World::GetEntities())
                {
                    auto* candidate = e->GetComponent<Terrain>();
                    float height = 0.0f;
                    if (candidate && candidate->SampleHeight(point.x, point.z, height) && fabsf(height - point.y) < 0.4f)
                    { terrain = candidate; break; }
                }
            }
            std::string layer;
            TerrainSurfaceSample sample;
            if (terrain && terrain->SampleSurface(point.x, point.z, sample))
            {
                layer = terrain->GetLayerRules()[std::min(sample.dominant_layer, terrain_layer_max - 1)].name;
                s.material = terrain->GetLayerMaterial(std::min(sample.dominant_layer, terrain_layer_max - 1));
                s.loose = 1.0f;
                // Flow/deposition describes damp hollows; standing water adds actual saturation.
                s.wet = std::clamp(terrain->GetWetness() + sample.flow * sample.deposition * 0.6f + wheel.water_depth * 80.0f, 0.0f, 1.0f);
            }
            else if (wheel.contact_surface == car::surface_grass) { layer = "grass"; s.loose = 1.0f; }
            else if (wheel.contact_surface == car::surface_dirt) { layer = "dirt"; s.loose = 1.0f; }
            else if (wheel.contact_surface == car::surface_gravel) { layer = "gravel"; s.loose = 0.5f; }
            std::transform(layer.begin(), layer.end(), layer.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (layer.find("grass") != std::string::npos || layer.find("moss") != std::string::npos)
            { s.color = Color(0.042f, 0.075f, 0.016f, 0.8f); s.grass = 1.0f; s.size = 0.035f; }
            else if (layer.find("sand") != std::string::npos)
            { s.color = Color(0.42f, 0.30f, 0.15f, 0.55f); s.size = 0.012f; }
            else if (layer.find("snow") != std::string::npos || layer.find("ice") != std::string::npos)
            { s.color = Color(0.72f, 0.79f, 0.85f, 0.7f); s.size = 0.035f; }
            else if (layer.find("rock") != std::string::npos || layer.find("gravel") != std::string::npos)
            { s.color = Color(0.19f, 0.17f, 0.14f, 0.45f); s.loose = 0.2f; s.size = 0.014f; }
            else if (layer.find("forest") != std::string::npos)
            { s.color = Color(0.08f, 0.05f, 0.018f, 0.8f); s.grass = 0.25f; }
            s.color.r *= 1.0f - s.wet * 0.55f;
            s.color.g *= 1.0f - s.wet * 0.55f;
            s.color.b *= 1.0f - s.wet * 0.55f;
            s.roughness -= s.wet * 0.55f;
            return s;
        }

        struct Triangle { Vector3 a, b, c; };
        struct Node { BoundingBox box; uint32_t first = 0, count = 0, left = 0, right = 0; };
        // Immutable after publication. Receivers of the same mesh share the expensive tree.
        struct CollisionMesh
        {
            std::atomic<bool> ready = false;
            std::vector<Triangle> triangles;
            std::vector<Node> nodes;
            uint32_t Build(uint32_t first, uint32_t count)
            {
                const uint32_t index = static_cast<uint32_t>(nodes.size());
                nodes.emplace_back();
                BoundingBox bounds;
                for (uint32_t i = first; i < first + count; ++i)
                {
                    Vector3 v[] = { triangles[i].a, triangles[i].b, triangles[i].c };
                    bounds.Merge(BoundingBox(v, 3));
                }
                nodes[index].box = bounds;
                if (count <= 8) { nodes[index].first = first; nodes[index].count = count; return index; }
                const Vector3 extent = bounds.GetExtents();
                const int axis = extent.x > extent.y && extent.x > extent.z ? 0 : (extent.y > extent.z ? 1 : 2);
                auto key = [axis](const Triangle& t) { auto p = t.a + t.b + t.c; return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
                const uint32_t half = count / 2;
                std::nth_element(triangles.begin() + first, triangles.begin() + first + half, triangles.begin() + first + count,
                    [&](const Triangle& a, const Triangle& b) { return key(a) < key(b); });
                const uint32_t left = Build(first, half);
                const uint32_t right = Build(first + half, count - half);
                nodes[index].left = left; nodes[index].right = right;
                return index;
            }
            void Trace(uint32_t index, const Ray& ray, float& distance, Vector3& normal) const
            {
                const auto& node = nodes[index];
                if (ray.HitDistance(node.box) > distance) return;
                if (!node.count) { Trace(node.left, ray, distance, normal); Trace(node.right, ray, distance, normal); return; }
                for (uint32_t i = node.first; i < node.first + node.count; ++i)
                {
                    const auto& t = triangles[i];
                    Vector3 n;
                    float hit = ray.HitDistance(t.a, t.b, t.c, &n);
                    // Imported body panels can have mirrored winding. The first physical surface still blocks spray.
                    if (!std::isfinite(hit)) hit = ray.HitDistance(t.c, t.b, t.a, &n);
                    if (hit < distance) { distance = hit; normal = n.Normalized(); }
                }
            }

            void Nearest(uint32_t index, const Vector3& query, const Matrix& world,
                float& distance_squared, Vector3& point, Vector3& normal) const
            {
                const auto& node = nodes[index];
                // World-space distances keep the search radius correct for scaled imported panels.
                if (((node.box * world).GetClosestPoint(query) - query).LengthSquared() >= distance_squared) return;
                if (!node.count)
                {
                    Nearest(node.left, query, world, distance_squared, point, normal);
                    Nearest(node.right, query, world, distance_squared, point, normal);
                    return;
                }
                for (uint32_t i = node.first; i < node.first + node.count; ++i)
                {
                    const auto& t = triangles[i];
                    const Vector3 a = world * t.a, b = world * t.b, c = world * t.c;
                    const Vector3 ab = b - a, bc = c - b, ca = a - c;
                    const Vector3 n = ab.Cross(c - a);
                    const float area_squared = n.LengthSquared();
                    if (area_squared < 1e-16f) continue;
                    Vector3 candidate = query - n * ((query - a).Dot(n) / area_squared);
                    if (ab.Cross(candidate - a).Dot(n) < 0.0f || bc.Cross(candidate - b).Dot(n) < 0.0f || ca.Cross(candidate - c).Dot(n) < 0.0f)
                    {
                        const Vector3 on_ab = a + ab * std::clamp((query - a).Dot(ab) / ab.LengthSquared(), 0.0f, 1.0f);
                        const Vector3 on_bc = b + bc * std::clamp((query - b).Dot(bc) / bc.LengthSquared(), 0.0f, 1.0f);
                        const Vector3 on_ca = c + ca * std::clamp((query - c).Dot(ca) / ca.LengthSquared(), 0.0f, 1.0f);
                        candidate = (on_ab - query).LengthSquared() < (on_bc - query).LengthSquared() ? on_ab : on_bc;
                        if ((on_ca - query).LengthSquared() < (candidate - query).LengthSquared()) candidate = on_ca;
                    }
                    const float distance = (candidate - query).LengthSquared();
                    if (distance < distance_squared)
                    {
                        distance_squared = distance;
                        point = candidate;
                        normal = n.Normalized();
                    }
                }
            }
        };
        struct Receiver
        {
            uint64_t entity_id = 0, mesh_id = 0;
            uint32_t sub_mesh = 0;
            std::shared_ptr<CollisionMesh> collision;
            Matrix previous, world, inverse, old_inverse;
            Render* frame_render = nullptr;
        };
        // Accessed only from the world thread; expired entries do not retain world assets.
        static std::map<std::pair<uint64_t, uint32_t>, std::weak_ptr<CollisionMesh>> collision_cache;

        struct Drop
        {
            Vector3 position, velocity;
            Surface surface;
            float age = 0.0f, seed = 0.0f;
            Vector3 ground_point = Vector3::Zero, ground_normal = Vector3::Up;
        };
    }

    struct CarSurfaceEffects::State
    {
        std::array<uint64_t, 8> emitters = {};
        std::array<float, 4> remainder = {};
        std::vector<Receiver> receivers;
        std::vector<Drop> drops;
        Vector3 previous_position;
        bool active = false;
        float scratch_cooldown = 0.0f;
        uint32_t random = 0x351a73u;
        float Random() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return (random & 0xffffff) / 16777216.0f; }

        void CacheReceivers(Entity* vehicle, Physics* physics)
        {
            SP_PROFILE_CPU();
            receivers.clear();
            std::erase_if(collision_cache, [](const auto& item) { return item.second.expired(); });
            struct BuildRequest { std::shared_ptr<Mesh> mesh; uint32_t sub_mesh; std::shared_ptr<CollisionMesh> collision; };
            std::vector<BuildRequest> builds;
            std::vector<Entity*> entities;
            vehicle->GetDescendants(&entities);
            entities.push_back(vehicle);
            for (Entity* entity : entities)
            {
                Render* render = entity->GetComponent<Render>();
                if (!render || !render->GetMesh()) continue;
                bool wheel_mesh = false;
                for (Entity* e = entity; e && e != vehicle; e = e->GetParent())
                    for (int i = 0; i < 4; ++i)
                        wheel_mesh |= e == physics->GetWheelEntity(static_cast<WheelIndex>(i));
                if (wheel_mesh) continue;
                Receiver receiver;
                receiver.entity_id = entity->GetObjectId();
                receiver.mesh_id = render->GetMesh()->GetObjectId();
                receiver.sub_mesh = render->GetSubMeshIndex();
                receiver.previous = entity->GetMatrix();
                auto& cached = collision_cache[{receiver.mesh_id, receiver.sub_mesh}];
                receiver.collision = cached.lock();
                if (!receiver.collision)
                {
                    // Pin only the asset, never an Entity/Render or this State, across the worker job.
                    auto mesh = std::static_pointer_cast<Mesh>(render->GetMesh()->weak_from_this().lock());
                    if (!mesh) continue;
                    receiver.collision = std::make_shared<CollisionMesh>();
                    cached = receiver.collision;
                    builds.push_back({std::move(mesh), receiver.sub_mesh, receiver.collision});
                }
                receivers.push_back(std::move(receiver));
            }
            if (!builds.empty())
            {
                // One job per car, not one job per panel: leave workers available for frame work.
                ThreadPool::AddTask([builds = std::move(builds)]()
                {
                    for (const auto& request : builds)
                    {
                        auto& collision = *request.collision;
                        std::vector<uint32_t> indices;
                        std::vector<RHI_Vertex_PosTexNorTan> vertices;
                        request.mesh->GetGeometry(request.sub_mesh, &indices, &vertices);
                        collision.triangles.reserve(indices.size() / 3);
                        for (size_t i = 0; i + 2 < indices.size(); i += 3)
                            if (indices[i] < vertices.size() && indices[i+1] < vertices.size() && indices[i+2] < vertices.size())
                                collision.triangles.push_back({vertices[indices[i]].get_position(), vertices[indices[i+1]].get_position(), vertices[indices[i+2]].get_position()});
                        if (!collision.triangles.empty())
                            collision.Build(0, static_cast<uint32_t>(collision.triangles.size()));
                        collision.ready.store(true, std::memory_order_release);
                    }
                });
            }
        }

        ParticleSystem* Emitter(Entity* vehicle, int index, bool dust)
        {
            Entity* e = World::GetEntityById(emitters[index]);
            if (!e)
            {
                e = World::CreateEntity();
                e->SetObjectName(dust ? "tire_dust" : "tire_debris");
                e->SetParent(vehicle);
                e->SetTransient(true);
                emitters[index] = e->GetObjectId();
                auto* p = e->AddComponent<ParticleSystem>();
                p->ApplyPreset(ParticlePreset::Dust);
                p->SetRenderMode(ParticleRenderMode::Billboard);
                p->SetLightingMode(ParticleLightingMode::Lit);
                p->SetBlendMode(ParticleBlendMode::Alpha);
                p->SetMaxParticles(dust ? 96 : 128);
                p->SetEmissionRate(0.0f);
                p->SetLifetime(dust ? 1.1f : 0.55f);
                p->SetEmissionRadius(dust ? 0.12f : 0.10f);
                p->SetDirectionalBlend(1.0f);
                p->SetEmissionConeAngle(dust ? 0.30f : 0.18f);
                p->SetVelocityInheritance(1.0f);
                p->SetGravityModifier(dust ? -0.2f : -1.0f);
                p->SetDrag(dust ? 1.4f : 0.8f);
                p->SetTurbulenceStrength(dust ? 0.12f : 0.0f);
                p->SetWindInfluence(dust ? 0.15f : 0.0f);
                p->SetEmissiveStrength(0.0f);
            }
            return e->GetComponent<ParticleSystem>();
        }

        void Scratch(Entity* vehicle, float dt)
        {
            scratch_cooldown = std::max(0.0f, scratch_cooldown - dt);
            if (scratch_cooldown > 0.0f) return;
            for (const auto& contact : PhysicsWorld::GetFrameContacts())
            {
                const bool is_a = contact.entity_a == vehicle && (contact.vehicle_chassis_mask & 1u);
                const bool is_b = contact.entity_b == vehicle && (contact.vehicle_chassis_mask & 2u);
                if (!is_a && !is_b) continue;
                const Vector3 outward = is_a ? -contact.normal : contact.normal;
                const Vector3 velocity = is_a ? contact.relative_velocity : -contact.relative_velocity;
                const float impact = std::max(0.0f, velocity.Dot(outward));
                const Vector3 slide = velocity - outward * velocity.Dot(outward);
                const float speed = slide.Length();
                // Resting support, wheel contacts and very gentle nudges do not damage paint.
                // CCD's first touch can have no reported impulse. Closing speed still identifies a hit.
                if (impact < 1.2f && (speed < 1.5f || contact.impulse.LengthSquared() < 4.0f)) continue;
                const Vector3 contact_position = vehicle->GetMatrix() * contact.chassis_local_position[is_a ? 0 : 1];
                Render* target = nullptr;
                Vector3 point, normal;
                float closest = 0.45f * 0.45f;
                for (const auto& receiver : receivers)
                {
                    Render* render = receiver.frame_render;
                    if (!render || !receiver.collision->ready.load(std::memory_order_acquire) || receiver.collision->nodes.empty()) continue;
                    float distance = closest;
                    Vector3 p, n;
                    receiver.collision->Nearest(0, contact_position, receiver.world, distance, p, n);
                    if (distance >= closest) continue;
                    closest = distance;
                    target = render;
                    point = p;
                    normal = n.Dot(outward) < 0.0f ? -n : n;
                }
                // Include glass/trim in the proximity query so a hit on them cannot jump to paint behind them.
                Material* material = target ? target->GetMaterial() : nullptr;
                if (!material || material->IsTransparent() || material->GetProperty(MaterialProperty::PaintPreset) <= 0.0f) continue;
                Vector3 tangent = slide - normal * slide.Dot(normal);
                if (tangent.LengthSquared() < 0.0001f) tangent = normal.Cross(fabsf(normal.y) < 0.9f ? Vector3::Up : Vector3::Right);
                tangent.Normalize();
                const float severity = std::clamp(std::max(impact, speed * 0.35f) / 12.0f, 0.0f, 1.0f);
                const Vector3 u = normal.Cross(tangent).Normalized() * (0.025f + severity * 0.085f);
                const Vector3 v = tangent * std::clamp(0.06f + impact * 0.008f + speed * 0.018f, 0.06f, 0.45f);
                const Vector3 w = normal * 0.045f;
                Matrix projector(u.x,u.y,u.z,0, v.x,v.y,v.z,0, w.x,w.y,w.z,0, point.x,point.y,point.z,1);
                DecalParameters decal;
                decal.world_to_decal = projector.Inverted();
                decal.kind = 1;
                decal.color = Vector4(0.24f, 0.25f, 0.27f, 0.65f + severity * 0.35f);
                decal.surface = Vector4(0.62f, -0.0003f, Random() * 100.0f, 0.0f);
                target->AddDecal(decal);
                scratch_cooldown = 0.06f;
                break;
            }
        }

        bool Deposit(const Drop& drop, const Vector3& next, float fraction_start, float fraction_end, float step)
        {
            float closest = 1.0f;
            Render* target = nullptr;
            Vector3 point, normal, impact_velocity;
            for (auto& receiver : receivers)
            {
                Render* render = receiver.frame_render;
                if (!render || !receiver.collision->ready.load(std::memory_order_acquire) || receiver.collision->nodes.empty()) continue;
                // Relative segment includes body translation/rotation, so sideways/yaw motion can catch spray.
                const Matrix& inverse = receiver.inverse;
                const Matrix& old_inverse = receiver.old_inverse;
                const Vector3 a_old = old_inverse * drop.position;
                const Vector3 b_old = old_inverse * next;
                const Vector3 a = a_old + (inverse * drop.position - a_old) * fraction_start;
                const Vector3 b = b_old + (inverse * next - b_old) * fraction_end;
                const float length = (b-a).Length();
                if (length < 0.000001f) continue;
                Ray ray(a, b-a);
                float distance = length * closest;
                Vector3 n;
                receiver.collision->Trace(0, ray, distance, n);
                if (distance >= length * closest) continue;
                closest = distance / length;
                target = render;
                impact_velocity = (receiver.world * b - receiver.world * a) / step;
                point = receiver.world * (a + ray.GetDirection() * distance);
                if (n.Dot(ray.GetDirection()) > 0.0f) n = -n;
                const Matrix normal_matrix = inverse.Transposed();
                normal = (normal_matrix * n - normal_matrix * Vector3::Zero).Normalized();

            }
            if (!target) return false;
            Vector3 tangent = impact_velocity - normal * impact_velocity.Dot(normal);
            if (tangent.LengthSquared() < 0.0001f) tangent = normal.Cross(fabsf(normal.y) < 0.9f ? Vector3::Up : Vector3::Right);
            tangent.Normalize();
            const Vector3 across = normal.Cross(tangent).Normalized();
            const float radius = 0.045f + 0.085f * (drop.seed / 100.0f) + drop.surface.wet * 0.035f;
            const float elongation = 1.0f + std::min(2.0f, impact_velocity.Length() * 0.055f) + drop.surface.grass;
            const Vector3 u = across * radius, v = tangent * (radius * elongation), w = normal * 0.055f;
            Matrix projector(u.x,u.y,u.z,0, v.x,v.y,v.z,0, w.x,w.y,w.z,0, point.x,point.y,point.z,1);
            DecalParameters decal;
            decal.world_to_decal = projector.Inverted();
            const Color c = drop.surface.color;
            decal.color = Vector4(c.r,c.g,c.b,c.a);
            decal.surface = Vector4(drop.surface.roughness, 0.0018f + drop.surface.wet * 0.002f, drop.seed, drop.surface.grass);
            target->AddDecal(decal, drop.surface.material);
            return true;
        }
    };

    CarSurfaceEffects::CarSurfaceEffects() : m_state(std::make_unique<State>()) {}
    CarSurfaceEffects::~CarSurfaceEffects() = default;

    void CarSurfaceEffects::Tick(Entity* vehicle, float delta_time, bool playing)
    {
        SP_PROFILE_CPU();
        auto& s = *m_state;
        auto* physics = vehicle->GetComponent<Physics>();
        auto* sim = physics ? physics->GetVehicleSimulation() : nullptr;
        if (!sim) return;
        for (uint64_t id : s.emitters)
            if (Entity* e = World::GetEntityById(id))
                if (auto* p = e->GetComponent<ParticleSystem>()) p->SetEmissionRate(0.0f);
        if (!playing || !physics->IsVehicleSimulationActive())
        {
            if (!playing)
                for (const auto& receiver : s.receivers)
                    if (Entity* e = World::GetEntityById(receiver.entity_id))
                        if (auto* r = e->GetComponent<Render>()) r->ClearDecals();
            s.active = false; s.scratch_cooldown = 0.0f; s.drops.clear(); s.remainder.fill(0.0f); return;
        }
        // Traffic vehicles can be simulated across the island. Build mesh BVHs and
        // trace deposits only near the camera; preserve existing dirt when out of range.
        if (auto* camera = World::GetCamera())
        {
            if ((camera->GetEntity()->GetPosition() - vehicle->GetPosition()).LengthSquared() > 60.0f * 60.0f)
            {
                s.drops.clear(); s.remainder.fill(0.0f);
                s.previous_position = vehicle->GetPosition();
                for (auto& receiver : s.receivers)
                    if (Entity* e = World::GetEntityById(receiver.entity_id)) receiver.previous = e->GetMatrix();
                return;
            }
        }
        if (!std::isfinite(delta_time) || delta_time <= 0.0f) return;
        const float dt = std::min(delta_time, 0.1f);
        const bool teleport = !s.active || (vehicle->GetPosition() - s.previous_position).LengthSquared() > 100.0f;
        const bool mesh_changed = std::any_of(s.receivers.begin(), s.receivers.end(), [](const Receiver& receiver)
        {
            Entity* e = World::GetEntityById(receiver.entity_id);
            Render* r = e ? e->GetComponent<Render>() : nullptr;
            return !r || !r->GetMesh() || r->GetMesh()->GetObjectId() != receiver.mesh_id || r->GetSubMeshIndex() != receiver.sub_mesh;
        });
        // Waking physics or teleporting changes transforms, not mesh topology.
        // Keep the trees instead of rebuilding them on every traffic activation.
        if (s.receivers.empty() || mesh_changed)
            s.CacheReceivers(vehicle, physics);
        if (teleport || mesh_changed)
        {
            s.scratch_cooldown = 0.0f;
            s.drops.clear(); s.remainder.fill(0.0f);
            for (auto& receiver : s.receivers)
                if (Entity* e = World::GetEntityById(receiver.entity_id)) receiver.previous = e->GetMatrix();
        }
        s.active = true; s.previous_position = vehicle->GetPosition();
        for (auto& receiver : s.receivers)
            if (Entity* e = World::GetEntityById(receiver.entity_id))
            {
                receiver.frame_render = e->GetComponent<Render>();
                receiver.world = e->GetMatrix();
                receiver.inverse = receiver.world.Inverted();
                receiver.old_inverse = receiver.previous.Inverted();
            }
        if (!teleport && !mesh_changed) s.Scratch(vehicle, dt);
        // Existing droplets land before this frame's newly launched particles; no instant appearance.
        const int steps = std::max(1, static_cast<int>(ceilf(dt * 120.0f)));
        const float step = dt / steps;
        for (int k = 0; k < steps; ++k)
        {
            for (size_t j = 0; j < s.drops.size();)
            {
                auto& drop = s.drops[j];
                drop.velocity.y -= 9.81f * step;
                drop.velocity *= expf(-0.8f * step);
                Vector3 next = drop.position + drop.velocity * step;
                const float ground_distance = (next - drop.ground_point).Dot(drop.ground_normal);
                const bool landed = ground_distance < 0.0f;
                if (landed)
                {
                    const float previous_distance = std::max(0.0f, (drop.position - drop.ground_point).Dot(drop.ground_normal));
                    next = drop.position + (next - drop.position) * (previous_distance / std::max(previous_distance - ground_distance, 0.00001f));
                }
                const bool hit = s.Deposit(drop, next, static_cast<float>(k) / steps, static_cast<float>(k+1) / steps, step);
                drop.position = next; drop.age += step;
                if (hit || landed || drop.age > 0.55f) { s.drops[j] = s.drops.back(); s.drops.pop_back(); }
                else ++j;
            }
        }
        for (auto& receiver : s.receivers)
            if (Entity* e = World::GetEntityById(receiver.entity_id)) receiver.previous = e->GetMatrix();
        for (int i = 0; i < 4; ++i)
        {
            const auto& wheel = sim->get_wheel_state(i);
            if (!wheel.grounded || wheel.tire_load < 50.0f) { s.remainder[i] = 0; continue; }
            const auto wi = static_cast<WheelIndex>(i);
            const Vector3 point = physics->GetWheelContactPoint(wi);
            const Vector3 normal = physics->GetWheelContactNormal(wi).Normalized();
            Surface surface = sample_surface(wheel, point);
            if (surface.loose <= 0.0f) { s.remainder[i] = 0; continue; }
            Vector3 axle = vehicle->GetRight();
            if (auto* body = sim->get_multibody_state().corners[i].wheel_body)
            { const auto axis = body->getGlobalPose().q.rotate(physx::PxVec3(1,0,0)); axle = Vector3(axis.x,axis.y,axis.z); }
            const Vector3 forward = axle.Cross(normal).Normalized();
            const Vector3 hub_velocity(wheel.hub_linear_velocity.x, wheel.hub_linear_velocity.y, wheel.hub_linear_velocity.z);
            const float tread = wheel.angular_velocity * std::max(wheel.effective_radius, physics->GetWheelRadius());
            const float longitudinal = hub_velocity.Dot(forward);
            const float lateral = hub_velocity.Dot(axle);
            const float slip_speed = fabsf(tread - longitudinal) + fabsf(lateral);
            const float motion = std::max(fabsf(tread), hub_velocity.Length());
            const float load = std::clamp(wheel.tire_load / 3500.0f, 0.0f, 1.5f);
            const float rate = std::clamp((motion - 1.0f) * 1.2f + slip_speed * 3.0f, 0.0f, 65.0f) * surface.loose * load;
            if (rate <= 0.0f) continue;
            // Tread ejects backward and upward; lateral slip throws across the car during a slide.
            const float sign = fabsf(tread) > 0.5f ? (tread > 0 ? 1.0f : -1.0f) : (longitudinal >= 0 ? 1.0f : -1.0f);
            Vector3 launch = -forward * sign * std::min(6.0f, motion * 0.16f + slip_speed * 0.10f)
                           - axle * std::clamp(lateral * 0.18f, -2.0f, 2.0f)
                           + normal * std::min(2.2f, 0.7f + motion * 0.035f + slip_speed * 0.04f);
            const float radius = std::max(wheel.effective_radius, physics->GetWheelRadius());
            const float width = physics->GetWheelWidth(wi);
            // Dirt stays on the tread into its trailing arc before centrifugal release.
            // Across-width release lets shoulder spray reach exterior sills instead of only wheel wells.
            const Vector3 origin = point + normal * (radius * 0.42f) - forward * sign * (radius * 0.82f);
            auto* debris = s.Emitter(vehicle, i, false);
            auto* dust = s.Emitter(vehicle, i + 4, true);
            for (auto* p : {debris, dust})
            {
                p->GetEntity()->SetPosition(origin);
                p->SetEmissionDirection(launch.Normalized());
                p->SetStartSpeed(launch.Length());
                p->SetGroundPlane(Vector4(normal.x, normal.y, normal.z, -normal.Dot(point)));
            }
            debris->SetEmissionRadius(width * 0.5f);
            debris->SetEmissionRate(rate);
            debris->SetStartColor(surface.color);
            debris->SetEndColor(Color(surface.color.r,surface.color.g,surface.color.b,0.0f));
            debris->SetStartSize(surface.size * 0.65f * (1.0f + surface.wet));
            debris->SetEndSize(surface.size * 0.65f);
            debris->SetVelocityStretch(surface.grass > 0.5f ? 0.3f : 0.08f);
            dust->SetEmissionRate(rate * (1.0f - surface.wet) * (1.0f - surface.grass * 0.8f) * 0.6f);
            dust->SetStartSpeed(launch.Length() * 0.25f);
            dust->SetStartSize(0.06f); dust->SetEndSize(0.35f);
            dust->SetStartColor(Color(surface.color.r,surface.color.g,surface.color.b,0.08f));
            dust->SetEndColor(Color(surface.color.r,surface.color.g,surface.color.b,0.0f));
            // A representative subset of the spray is traced against a cached triangle BVH.
            // The GPU keeps the full plume; deposition cost stays bounded even in a four-wheel burnout.
            s.remainder[i] += std::min(rate * 0.25f, 12.0f) * dt;
            while (s.remainder[i] >= 1.0f && s.drops.size() < 96)
            {
                s.remainder[i] -= 1.0f;
                const Vector3 jitter = axle * ((s.Random()-0.5f) * 0.36f) + normal * ((s.Random()-0.5f) * 0.16f);
                const Vector3 velocity = (launch.Normalized() + jitter).Normalized() * launch.Length() * (0.7f + s.Random()*0.5f) + hub_velocity;
                s.drops.push_back({origin + axle * ((s.Random()-0.5f) * width),velocity,surface,0.0f,s.Random()*100.0f,point,normal});
            }
            s.remainder[i] = std::min(s.remainder[i], 1.0f);
        }
    }
}
