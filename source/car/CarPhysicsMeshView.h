/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "../geometry/Mesh.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Render.h"
#include <unordered_map>

namespace spartan
{
    // A transient render-only assembly. The solver remains the authority for every
    // rigid part; no visual entity has a Physics component or enters a saved world.
    class CarPhysicsMeshView
    {
        using Vector3 = math::Vector3;
        using Quaternion = math::Quaternion;
        struct Geometry
        {
            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;

            void triangle(const Vector3& a, const Vector3& b, const Vector3& c)
            {
                const Vector3 normal = Vector3::Cross(b - a, c - a).Normalized();
                const Vector3 tangent = (b - a).Normalized();
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.emplace_back(a, math::Vector2(0, 0), normal, tangent);
                vertices.emplace_back(b, math::Vector2(1, 0), normal, tangent);
                vertices.emplace_back(c, math::Vector2(0, 1), normal, tangent);
                indices.insert(indices.end(), {base, base + 1, base + 2});
            }

            // Closed surface of revolution about local X. Profiles run along the
            // outside from -X to +X, then return along the inside for hollow parts.
            void lathe(const std::vector<math::Vector2>& profile, uint32_t segments = 96)
            {
                for (size_t row = 0; row + 1 < profile.size(); ++row)
                {
                    const auto a = profile[row];
                    const auto b = profile[row + 1];
                    const float dx = b.x - a.x, dr = b.y - a.y;
                    const float length = sqrtf(dx * dx + dr * dr);
                    if (length < 0.000001f) continue;
                    const uint32_t base = static_cast<uint32_t>(vertices.size());
                    for (uint32_t i = 0; i <= segments; ++i)
                    {
                        const float u = static_cast<float>(i) / segments;
                        const float angle = u * 2.0f * math::pi;
                        const float c = cosf(angle), s = sinf(angle);
                        const Vector3 normal(-dr / length, dx * c / length, dx * s / length);
                        for (const auto point : {a, b})
                            vertices.emplace_back(Vector3(point.x, point.y * c, point.y * s), math::Vector2(u, point.x), normal, Vector3(0, -s, c));
                        if (i < segments)
                        {
                            const uint32_t v = base + i * 2;
                            indices.insert(indices.end(), {v, v + 2, v + 1, v + 1, v + 2, v + 3});
                        }
                    }
                }
            }

            std::shared_ptr<Mesh> finish()
            {
                auto mesh = std::make_shared<Mesh>();
                mesh->SetObjectName("Physics assembly mesh");
                mesh->AddGeometry(vertices, indices, false);
                mesh->CreateGpuBuffers();
                return mesh;
            }
        };

        struct Part { uint64_t id = 0; std::string geometry; bool used = false; };
        std::unordered_map<std::string, Part> parts;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
        std::shared_ptr<Material> steel, alloy, rubber, spring, chrome, brake, copper, electrical, charge;
        void draw_systems(Entity* root, Entity* vehicle, CarPhysics& physics, ::car::Simulation& simulation);
        uint64_t root_id = 0;
        uint32_t primitive_index = 0;

        static Vector3 vector(const physx::PxVec3& v) { return Vector3(v.x, v.y, v.z); }

        static std::string shape_key(const physx::PxGeometry& geometry)
        {
            std::string key = std::to_string(static_cast<int>(geometry.getType()));
            auto scalar = [&](float v) { key += "/" + std::to_string(v); };
            if (geometry.getType() == physx::PxGeometryType::eBOX)
            {
                const auto h = static_cast<const physx::PxBoxGeometry&>(geometry).halfExtents;
                scalar(h.x); scalar(h.y); scalar(h.z);
            }
            else if (geometry.getType() == physx::PxGeometryType::eCAPSULE)
            {
                const auto& capsule = static_cast<const physx::PxCapsuleGeometry&>(geometry);
                scalar(capsule.radius); scalar(capsule.halfHeight);
            }
            else if (geometry.getType() == physx::PxGeometryType::eSPHERE)
                scalar(static_cast<const physx::PxSphereGeometry&>(geometry).radius);
            else
            {
                // Content, not a raw shape pointer: PhysX can reuse allocations when
                // presets or simulation modes rebuild the assembly in place.
                const auto& convex = static_cast<const physx::PxConvexMeshGeometry&>(geometry);
                for (physx::PxU32 i = 0; i < convex.convexMesh->getNbVertices(); ++i)
                {
                    const auto v = convex.scale.transform(convex.convexMesh->getVertices()[i]);
                    scalar(v.x); scalar(v.y); scalar(v.z);
                }
                for (physx::PxU32 i = 0; i < convex.convexMesh->getNbPolygons(); ++i)
                {
                    physx::PxHullPolygon polygon;
                    if (!convex.convexMesh->getPolygonData(i, polygon)) continue;
                    key += ";";
                    for (physx::PxU32 j = 0; j < polygon.mNbVerts; ++j)
                        key += "/" + std::to_string(convex.convexMesh->getIndexBuffer()[polygon.mIndexBase + j]);
                }
            }
            return "shape/" + key;
        }

        static std::shared_ptr<Material> material(const char* name, const Color& color, float metalness, float roughness)
        {
            auto result = std::make_shared<Material>();
            result->SetObjectName(name);
            result->SetColor(color);
            result->SetProperty(MaterialProperty::Metalness, metalness);
            result->SetProperty(MaterialProperty::Roughness, roughness);
            return result;
        }

        template<typename Builder>
        void draw(Entity* root, const std::string& name, const std::string& geometry,
            const Vector3& position, const Quaternion& rotation, const Vector3& scale,
            const std::shared_ptr<Material>& surface, Builder&& build)
        {
            Part& part = parts[name];
            part.used = true;
            Entity* entity = part.id ? World::GetEntityById(part.id) : nullptr;
            if (!entity)
            {
                entity = World::CreateEntity();
                entity->SetObjectName(name);
                entity->SetTransient(true);
                entity->SetParent(root);
                auto* render = entity->AddComponent<Render>();
                render->SetSpatiallyDynamic(true);
                render->SetFlag(RenderFlags::ExcludeFromTerrainBlend);
                render->SetMaterial(surface);
                part.id = entity->GetObjectId();
                part.geometry.clear();
            }
            if (part.geometry != geometry)
            {
                auto& mesh = meshes[geometry];
                if (!mesh) mesh = build();
                entity->GetComponent<Render>()->SetOwnedMesh(mesh);
                part.geometry = geometry;
            }
            if (entity->GetComponent<Render>()->GetMaterial() != surface.get())
                entity->GetComponent<Render>()->SetMaterial(surface);
            entity->SetPositionAndRotation(position, rotation);
            entity->SetScale(scale);
            entity->SetActive(true);
        }

        static std::shared_ptr<Mesh> shape_mesh(const physx::PxGeometry& shape)
        {
            Geometry out;
            if (shape.getType() == physx::PxGeometryType::eBOX)
            {
                const auto h = static_cast<const physx::PxBoxGeometry&>(shape).halfExtents;
                const Vector3 v[] = {{-h.x,-h.y,-h.z},{h.x,-h.y,-h.z},{h.x,h.y,-h.z},{-h.x,h.y,-h.z},
                                    {-h.x,-h.y,h.z},{h.x,-h.y,h.z},{h.x,h.y,h.z},{-h.x,h.y,h.z}};
                const int faces[][4] = {{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
                for (const auto& f : faces)
                {
                    out.triangle(v[f[0]], v[f[1]], v[f[2]]);
                    out.triangle(v[f[0]], v[f[2]], v[f[3]]);
                }
            }
            else if (shape.getType() == physx::PxGeometryType::eCONVEXMESH)
            {
                const auto& convex = static_cast<const physx::PxConvexMeshGeometry&>(shape);
                const auto* mesh = convex.convexMesh;
                for (physx::PxU32 i = 0; i < mesh->getNbPolygons(); ++i)
                {
                    physx::PxHullPolygon polygon;
                    if (!mesh->getPolygonData(i, polygon)) continue;
                    const auto* indices = mesh->getIndexBuffer() + polygon.mIndexBase;
                    auto point = [&](int j) { return vector(convex.scale.transform(mesh->getVertices()[indices[j]])); };
                    for (int j = 1; j + 1 < polygon.mNbVerts; ++j)
                        out.triangle(point(0), point(j), point(j + 1));
                }
            }
            else
            {
                const bool capsule = shape.getType() == physx::PxGeometryType::eCAPSULE;
                const float radius = capsule ? static_cast<const physx::PxCapsuleGeometry&>(shape).radius : static_cast<const physx::PxSphereGeometry&>(shape).radius;
                const float half = capsule ? static_cast<const physx::PxCapsuleGeometry&>(shape).halfHeight : 0.0f;
                std::vector<math::Vector2> profile;
                for (int i = 0; i <= 32; ++i)
                {
                    const float a = -math::pi * 0.5f + math::pi * static_cast<float>(i) / 32.0f;
                    profile.emplace_back((i <= 16 ? -half : half) + radius * sinf(a), radius * cosf(a));
                    if (i == 16 && capsule) profile.emplace_back(half, radius);
                }
                out.lathe(profile, 48);
            }
            return out.finish();
        }

    public:
        void clear()
        {
            if (Entity* root = World::GetEntityById(root_id))
            {
                root->SetActive(false);
                World::RemoveEntity(root);
            }
            root_id = 0;
            parts.clear();
            meshes.clear();
        }

        void tick(Entity* vehicle, CarPhysics& physics, ::car::Simulation& simulation)
        {
            Entity* root = root_id ? World::GetEntityById(root_id) : nullptr;
            if (!root)
            {
                parts.clear();
                root = World::CreateEntity();
                root->SetObjectName("Physics meshes");
                root->SetTransient(true);
                root->SetParent(vehicle);
                root_id = root->GetObjectId();
                steel = material("Suspension steel", Color(0.12f, 0.14f, 0.17f, 1), 0.95f, 0.3f);
                alloy = material("Machined aluminium", Color(0.62f, 0.65f, 0.68f, 1), 1, 0.24f);
                rubber = material("Tire rubber", Color(0.018f, 0.021f, 0.024f, 1), 0, 0.86f);
                spring = material("Coil spring enamel", Color(0.75f, 0.16f, 0.025f, 1), 0.3f, 0.23f);
                spring->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                chrome = material("Polished damper rod", Color(0.7f, 0.72f, 0.75f, 1), 1, 0.12f);
                brake = material("Brake disc iron", Color(0.28f, 0.29f, 0.30f, 1), 1, 0.42f);
                copper = material("Motor windings", Color(0.72f, 0.28f, 0.09f, 1), 1, 0.27f);
                electrical = material("High voltage insulation", Color(0.95f, 0.22f, 0.015f, 1), 0, 0.45f);
                charge = material("Battery state", Color(0.05f, 0.55f, 0.22f, 1), 0.4f, 0.3f);
            }
            // Retired actors and optional parts disappear even when a simulation is rebuilt.
            for (auto& [name, part] : parts) part.used = false;
            primitive_index = 0;

            auto position = [&](const physx::PxVec3& p) { return physics.TransformVehiclePointToRender(PhysicsWorld::ToWorldPosition(vector(p))); };
            auto rotation = [&](const physx::PxQuat& q) { return physics.TransformVehicleRotationToRender(Quaternion(q.x, q.y, q.z, q.w)); };
            const auto& multibody = simulation.get_multibody_state();
            const auto& config = simulation.get_config();
            const bool full = physics.GetVehicleSimMode() != VehicleSimMode::Cheap && simulation.has_multibody();
            draw_systems(root, vehicle, physics, simulation);
            for (int i = 0; i < 4; ++i)
            {
                const auto& corner = multibody.corners[i];
                Entity* wheel = physics.GetWheelEntity(static_cast<WheelIndex>(i));
                if (!wheel || (full && !corner.wheel_body)) continue;
                const Vector3 center = full ? position(corner.wheel_body->getGlobalPose().p) : wheel->GetPosition();
                const Quaternion spin = full ? rotation(corner.wheel_body->getGlobalPose().q) : wheel->GetRotation();
                const float radius = config.wheel_radius_for(i), width = config.wheel_width_for(i);
                const Vector3 size(width, radius, radius);
                const std::string prefix = "Wheel " + std::to_string(i);
                draw(root, prefix + " tire", "tire", center, spin, size, rubber, []
                {
                    Geometry g;
                    // Rounded sidewalls and four recessed circumferential tread grooves.
                    std::vector<math::Vector2> p = {{-0.45f,0.68f},{-0.49f,0.74f},{-0.5f,0.84f},{-0.48f,0.93f},{-0.42f,0.98f},{-0.36f,1.0f}};
                    for (float x : {-0.27f, -0.09f, 0.09f, 0.27f})
                    {
                        p.emplace_back(x - 0.016f, 1.0f); p.emplace_back(x - 0.012f, 0.982f);
                        p.emplace_back(x + 0.012f, 0.982f); p.emplace_back(x + 0.016f, 1.0f);
                    }
                    p.insert(p.end(), {{0.36f,1.0f},{0.42f,0.98f},{0.48f,0.93f},{0.5f,0.84f},{0.49f,0.74f},{0.45f,0.68f},{-0.45f,0.68f}});
                    g.lathe(p, 128); return g.finish();
                });
                draw(root, prefix + " rim", "rim", center, spin, size, alloy, []
                {
                    Geometry g;
                    g.lathe({{-0.45f,0.65f},{-0.45f,0.7f},{-0.40f,0.71f},{0.40f,0.71f},{0.45f,0.7f},{0.45f,0.65f},{-0.45f,0.65f}});
                    // Ten open spokes on each face; spaces reveal the rotating disc.
                    for (int side : {-1, 1}) for (int spoke = 0; spoke < 10; ++spoke)
                    {
                        const float angle = static_cast<float>(spoke) * math::pi * 0.2f;
                        auto point = [&](float x, float r, float a) { return Vector3(x, r * cosf(a), r * sinf(a)); };
                        Vector3 v[8];
                        for (int k = 0; k < 8; ++k)
                            v[k] = point(static_cast<float>(side) * (k < 4 ? 0.40f : 0.34f), (k % 4 < 2 ? 0.18f : 0.67f), angle + ((k % 4 == 0 || k % 4 == 3) ? -0.045f : 0.045f));
                        const int faces[][4] = {{0,1,2,3},{7,6,5,4},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
                        for (const auto& f : faces)
                        {
                            if (side > 0) { g.triangle(v[f[0]],v[f[2]],v[f[1]]); g.triangle(v[f[0]],v[f[3]],v[f[2]]); }
                            else { g.triangle(v[f[0]],v[f[1]],v[f[2]]); g.triangle(v[f[0]],v[f[2]],v[f[3]]); }
                        }
                    }
                    g.lathe({{-0.43f,0.0f},{-0.43f,0.19f},{0.43f,0.19f},{0.43f,0.0f},{-0.43f,0.0f}}, 64);
                    return g.finish();
                });
                draw(root, prefix + " brake disc", "disc", center, spin, size, brake, []
                {
                    Geometry g;
                    g.lathe({{-0.06f,0.2f},{-0.06f,0.62f},{0.06f,0.62f},{0.06f,0.2f},{-0.06f,0.2f}});
                    return g.finish();
                });
            }
        }

        // These entry points are called by the skeleton traversal itself. This is
        // intentionally not another list of car parts: every structural primitive
        // added to debug drawing automatically participates in the mesh view.
        std::shared_ptr<Material> surface(const Color& color) const
        {
            if (color.b > color.r * 1.4f) return chrome;
            if (color.r > color.b * 1.4f) return spring;
            if (color.g > color.r * 1.4f) return alloy;
            return steel;
        }

        template<typename Builder>
        void primitive(const std::string& key, const Vector3& position, const Quaternion& rotation,
            const Vector3& scale, const Color& color, Builder&& build)
        {
            if (Entity* root = World::GetEntityById(root_id))
                draw(root, "Skeleton part " + std::to_string(primitive_index++), key, position, rotation, scale, surface(color), std::forward<Builder>(build));
        }

        void sphere(const Vector3& center, float radius, const Color& color)
        {
            if (radius <= 0.0f) return;
            primitive("unit sphere", center, Quaternion::Identity, Vector3(radius, radius, radius), color,
                [] { return shape_mesh(physx::PxSphereGeometry(1.0f)); });
        }

        void cylinder(const Vector3& start, const Vector3& end, float radius, const Color& color)
        {
            const float length = (end - start).Length();
            if (length <= 0.001f || radius <= 0.0f) return;
            primitive("unit cylinder", start, Quaternion::FromRotation(Vector3::Right, (end - start) / length),
                Vector3(length, radius, radius), color, []
            {
                Geometry g;
                g.lathe({{0.0f,0.0f},{0.0f,1.0f},{1.0f,1.0f},{1.0f,0.0f}}, 64);
                return g.finish();
            });
        }

        template<typename Transform>
        void shape(const physx::PxTransform& pose, const physx::PxGeometry& geometry, const Color& color, Transform&& to_render)
        {
            if (geometry.getType() != physx::PxGeometryType::eBOX && geometry.getType() != physx::PxGeometryType::eCAPSULE &&
                geometry.getType() != physx::PxGeometryType::eSPHERE && geometry.getType() != physx::PxGeometryType::eCONVEXMESH) return;
            const Vector3 center = to_render(pose.p);
            Quaternion orientation;
            orientation.FromAxes(
                (to_render(pose.transform(physx::PxVec3(1,0,0))) - center).Normalized(),
                (to_render(pose.transform(physx::PxVec3(0,1,0))) - center).Normalized(),
                (to_render(pose.transform(physx::PxVec3(0,0,1))) - center).Normalized());
            primitive(shape_key(geometry), center, orientation, Vector3::One, color, [&] { return shape_mesh(geometry); });
        }

        void coil(const Vector3& start, const Vector3& end, const Vector3& reference, float radius, const Color& color)
        {
            const float length = (end - start).Length();
            if (length <= 0.001f) return;
            const Vector3 axis = (end - start) / length;
            Vector3 tangent = reference - axis * Vector3::Dot(reference, axis);
            if (tangent.LengthSquared() <= 0.0001f)
                tangent = Vector3::Right - axis * Vector3::Dot(Vector3::Right, axis);
            tangent.Normalize();
            Quaternion orientation;
            orientation.FromAxes(axis, tangent, Vector3::Cross(axis, tangent).Normalized());
            // Same six turns, reference direction and sin(pi*t) envelope as the
            // debug spring, sampled densely for a smooth solid wire.
            primitive("unit coil", start, orientation, Vector3(length, radius, radius), color, []
            {
                Geometry g;
                constexpr uint32_t steps = 192, sides = 12;
                for (uint32_t step = 0; step <= steps; ++step)
                {
                    const float t = static_cast<float>(step) / steps;
                    const float a = t * 12.0f * math::pi;
                    const float envelope = sinf(t * math::pi);
                    const Vector3 radial(0, cosf(a), sinf(a));
                    const Vector3 center(t, radial.y * envelope, radial.z * envelope);
                    const float derivative = math::pi * cosf(t * math::pi);
                    const Vector3 along = Vector3(1, derivative * cosf(a) - envelope * sinf(a) * 12 * math::pi,
                        derivative * sinf(a) + envelope * cosf(a) * 12 * math::pi).Normalized();
                    const Vector3 across = Vector3::Cross(along, radial).Normalized();
                    const Vector3 normal_axis = Vector3::Cross(across, along).Normalized();
                    for (uint32_t side = 0; side <= sides; ++side)
                    {
                        const float b = static_cast<float>(side) / sides * 2 * math::pi;
                        const Vector3 normal = normal_axis * cosf(b) + across * sinf(b);
                        // A small elliptical section becomes approximately round at
                        // normal suspension lengths, without rebuilding the mesh each frame.
                        const Vector3 offset(normal.x * 0.014f, normal.y * 0.10f, normal.z * 0.10f);
                        g.vertices.emplace_back(center + offset, math::Vector2(t, static_cast<float>(side) / sides), normal, along);
                        if (step < steps && side < sides)
                        {
                            const uint32_t v = step * (sides + 1) + side;
                            g.indices.insert(g.indices.end(), {v,v+1,v+sides+1,v+1,v+sides+2,v+sides+1});
                        }
                    }
                }
                return g.finish();
            });
        }

        void end()
        {
            for (const auto& [name, part] : parts)
                if (!part.used)
                    if (Entity* entity = World::GetEntityById(part.id)) entity->SetActive(false);
        }
    };
}
