// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// Scattered trees that break when something hits them hard enough. Every tree has a real trunk
// diameter, measured from its mesh, and green wood properties. A hit loads the trunk like a
// cantilever on a root plate: whichever gives first, the stem (modulus of rupture) or the roots
// (anchorage), decides the failure. Below that the tree only sways at its natural frequency.
// A fallen tree stays on the ground and stands up again once nobody is around to see it.
#pragma once
#include "VegetationInteraction.h"
#include "../commands/console/ConsoleCommands.h"
#include "../world/components/Render.h"
#include "../geometry/Mesh.h"
#include "../rhi/RHI_Vertex.h"
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace spartan::breakable_trees
{
    using namespace math;

    inline TConsoleVar<float> cvar_respawn_distance("game.tree_respawn_distance", 120.0f,
        "metres from the camera beyond which a knocked over tree stands up again");
    inline TConsoleVar<float> cvar_strength_scale("game.tree_strength_scale", 1.0f,
        "scales wood strength and root anchorage, 1 is green wood as measured");

    // green (unseasoned) wood, the state of a living tree
    struct Wood
    {
        float strength;  // modulus of rupture, Pa
        float stiffness; // modulus of elasticity, Pa
        float density;   // kg/m^3
        float anchorage; // critical root turning moment per kg of stem, N m/kg
    };
    constexpr Wood wood_pine  = { 45.0e6f, 8.0e9f, 850.0f, 145.0f };
    constexpr Wood wood_olive = { 70.0e6f, 9.5e9f, 1050.0f, 170.0f };

    constexpr float pi              = 3.14159265f;
    constexpr float gravity         = 9.81f;
    constexpr float breast_height   = 1.3f;  // metres, where foresters measure a trunk
    constexpr float stem_form       = 0.45f; // stem volume over the cylinder of its base diameter
    constexpr float crown_mass      = 1.25f; // branches and foliage on top of the stem
    constexpr float root_yield      = 0.12f; // rad, root plate rotation at the peak turning moment
    constexpr float root_tip        = 0.5f;  // rad, root plate rotation it takes to go over, as work per moment
    constexpr float stem_hinge      = 0.85f; // plastic section factor 1.7 times a 0.5 rad hinge, as work per moment
    constexpr float sway_damping    = 0.08f; // damping ratio of a standing tree
    constexpr float index_cell      = 8.0f;  // metres per spatial index cell
    constexpr float lookahead       = 0.1f;  // seconds, the trunk has to go before the solver sees the contact
    constexpr uint32_t fallen_max   = 256;   // oldest resting trees stand up first beyond this

    struct Tree
    {
        Vector3 base;
        float height   = 0.0f;
        float diameter = 0.0f;
    };

    // one tile of one tree variant, every instance index is one tree across all of its parts
    // a single stand is one placed tree entity, its renders are not instanced and it is tree 0
    struct Stand
    {
        uint64_t root_id = 0;
        std::vector<uint64_t> part_ids;
        std::vector<Tree> trees;
        std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
        std::unordered_set<uint32_t> fallen;
        std::unordered_map<uint32_t, float> hit_time;
        Wood wood    = wood_pine;
        bool indexed = false;
        bool single  = false;
        // a placed tree authored without collision gets a trunk, it leaves the scene when the tree falls
        physx::PxRigidStatic* collider = nullptr;
        float collider_length          = 0.0f;
    };

    // a tree that moves, falling over or swaying back after a hit it survived
    struct Motion
    {
        uint64_t stand_id = 0;
        uint32_t instance = 0;
        Instance original;
        Vector3 original_position;
        Quaternion original_rotation;
        Vector3 pivot;
        Vector3 axis;
        float height           = 1.0f;
        float angle            = 0.0f;
        float angular_velocity = 0.0f;
        float angle_rest       = 1.5708f;
        float age              = 0.0f;
        uint32_t bounces       = 0;
        bool falling           = true;
        bool resting           = false;
        // sway
        float amplitude = 0.0f;
        float omega     = 0.0f;
    };

    inline std::unordered_map<uint64_t, Stand> stands;
    inline std::vector<Motion> motions;
    // per mesh, at breast height, mesh units, with the count of bark vertices that agree on it
    struct TrunkSection
    {
        float radius     = 0.0f;
        uint32_t support = 0;
    };
    inline std::unordered_map<uint64_t, TrunkSection> trunk_radius_cache;
    inline size_t scanned_entity_count = 0;
    inline float scan_timer            = 0.0f;
    inline float clock                 = 0.0f;
    inline physx::PxMaterial* bark     = nullptr;

    inline uint64_t cell_key(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
    }

    inline float series(float a, float b)
    {
        return (a > 0.0f && b > 0.0f) ? a * b / (a + b) : 0.0f;
    }

    inline void for_each_part(const Stand& stand, const std::function<void(Entity*, Render*)>& callback)
    {
        for (uint64_t id : stand.part_ids)
        {
            Entity* part = World::GetEntityById(id);
            Render* render = part ? part->GetComponent<Render>() : nullptr;
            if (render && render->HasInstancing() != stand.single)
                callback(part, render);
        }
    }

    inline uint32_t tree_count(const Stand& stand)
    {
        if (stand.single)
            return 1;
        Entity* first = World::GetEntityById(stand.part_ids.front());
        Render* render = first ? first->GetComponent<Render>() : nullptr;
        return render ? render->GetInstanceCount() : 0;
    }

    inline void collider_enable(Stand& stand, bool enabled)
    {
        if (!stand.collider || stand.trees.empty())
            return;
        std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        if (enabled)
        {
            // a removed actor misses origin shifts, place it again before it goes back in
            const Vector3 center = PhysicsWorld::ToPhysicsPosition(stand.trees.front().base + Vector3(0.0f, stand.collider_length * 0.5f, 0.0f));
            stand.collider->setGlobalPose(physx::PxTransform(physx::PxVec3(center.x, center.y, center.z)));
            PhysicsWorld::AddActor(stand.collider);
        }
        else
        {
            PhysicsWorld::RemoveActor(stand.collider);
        }
    }

    inline void collider_release(Stand& stand)
    {
        if (!stand.collider)
            return;
        std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        PhysicsWorld::RemoveActor(stand.collider);
        stand.collider->release();
        stand.collider = nullptr;
    }

    // a vertical capsule over the lower trunk, what a bumper or a shoulder actually meets
    inline void collider_create(Stand& stand, Entity* root)
    {
        auto* physics = static_cast<physx::PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics || stand.collider || stand.trees.empty())
            return;
        const Tree& tree = stand.trees.front();
        const float radius = std::max(tree.diameter * 0.5f, 0.03f);
        const float length = std::clamp(tree.height * 0.5f, 1.0f, 4.0f);
        const float half   = std::max(length * 0.5f - radius, 0.05f);
        stand.collider_length = length;

        std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        if (!bark)
            bark = physics->createMaterial(0.8f, 0.7f, 0.05f);
        const Vector3 center = PhysicsWorld::ToPhysicsPosition(tree.base + Vector3(0.0f, length * 0.5f, 0.0f));
        stand.collider = physics->createRigidStatic(physx::PxTransform(physx::PxVec3(center.x, center.y, center.z)));
        if (!stand.collider)
            return;
        if (physx::PxShape* shape = physics->createShape(physx::PxCapsuleGeometry(radius, half), *bark))
        {
            shape->setLocalPose(physx::PxTransform(physx::PxVec3(0.0f), physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
            stand.collider->attachShape(*shape);
            shape->release();
        }
        stand.collider->userData = reinterpret_cast<void*>(root);
        PhysicsWorld::AddActor(stand.collider);
    }

    // tree layers tag their variant roots, the scatter can rebuild them at any time so this is cheap to redo
    // a placed tree opts in with the breakable_tree tag on its root
    inline void Scan()
    {
        const std::vector<Entity*>& entities = World::GetEntities();
        scanned_entity_count = entities.size();
        std::unordered_set<uint64_t> alive;
        for (Entity* entity : entities)
        {
            if (!entity || (!entity->HasTag("terrain_tree") && !entity->HasTag("breakable_tree")))
                continue;
            const uint64_t id = entity->GetObjectId();
            alive.insert(id);
            if (stands.count(id))
                continue;

            Stand stand;
            stand.root_id = id;
            stand.wood    = entity->GetObjectName().find("olive") != std::string::npos ? wood_olive : wood_pine;
            std::vector<Entity*> parts = { entity };
            entity->GetDescendants(&parts);
            std::vector<uint64_t> placed;
            for (Entity* part : parts)
            {
                Render* render = part->GetComponent<Render>();
                if (!render)
                    continue;
                if (render->HasInstancing())
                    stand.part_ids.push_back(part->GetObjectId());
                else
                    placed.push_back(part->GetObjectId());
            }
            if (stand.part_ids.empty() && !placed.empty())
            {
                stand.part_ids = std::move(placed);
                stand.single   = true;
            }
            if (!stand.part_ids.empty())
                stands.emplace(id, std::move(stand));
        }
        for (auto it = stands.begin(); it != stands.end();)
        {
            if (alive.count(it->first))
            {
                ++it;
                continue;
            }
            collider_release(it->second);
            it = stands.erase(it);
        }
        motions.erase(std::remove_if(motions.begin(), motions.end(), [](const Motion& motion) { return !stands.count(motion.stand_id); }), motions.end());
    }

    inline bool stand_bounds(const Stand& stand, BoundingBox& bounds)
    {
        bool any = false;
        bounds = BoundingBox(Vector3::Infinity, Vector3::InfinityNeg);
        for_each_part(stand, [&](Entity*, Render* render)
        {
            bounds.Merge(render->GetBoundingBox());
            any = true;
        });
        return any;
    }

    // the trunk radius of a mesh at breast height, from the vertices of the band around it
    inline TrunkSection measure_trunk_section(Render* render)
    {
        const uint64_t key = reinterpret_cast<uint64_t>(render->GetMesh()) ^ (static_cast<uint64_t>(render->GetSubMeshIndex()) << 56);
        auto cached = trunk_radius_cache.find(key);
        if (cached != trunk_radius_cache.end())
            return cached->second;

        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        render->GetGeometry(nullptr, &vertices);
        float radius     = 0.0f;
        uint32_t support = 0;
        if (!vertices.empty())
        {
            float bottom = vertices.front().pos[1];
            for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
                bottom = std::min(bottom, vertex.pos[1]);

            // breast height first, the flare at the root and the first branches both lie
            std::vector<const RHI_Vertex_PosTexNorTan*> band;
            for (float band_low : { breast_height - 0.3f, 0.3f })
            {
                band.clear();
                for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
                {
                    const float y = vertex.pos[1] - bottom;
                    if (y >= band_low && y <= band_low + 0.6f)
                        band.push_back(&vertex);
                }
                if (band.size() >= 8)
                    break;
            }

            if (band.size() >= 8)
            {
                // start from the median, a branch pulls a mean off the stem axis
                std::vector<float> xs, zs;
                for (const RHI_Vertex_PosTexNorTan* vertex : band)
                {
                    xs.push_back(vertex->pos[0]);
                    zs.push_back(vertex->pos[2]);
                }
                std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
                std::nth_element(zs.begin(), zs.begin() + zs.size() / 2, zs.end());
                float cx = xs[xs.size() / 2];
                float cz = zs[zs.size() / 2];

                // bark faces away from the stem axis, branches leaving the band do not, so only
                // radially facing vertices count and the axis is refined from them
                std::vector<float> radii;
                for (uint32_t pass = 0; pass < 3; ++pass)
                {
                    radii.clear();
                    float sum_x = 0.0f, sum_z = 0.0f;
                    for (const RHI_Vertex_PosTexNorTan* vertex : band)
                    {
                        const Vector3 normal = vertex->get_normal();
                        const float dx = vertex->pos[0] - cx;
                        const float dz = vertex->pos[2] - cz;
                        const float r  = std::sqrt(dx * dx + dz * dz);
                        const float nh = std::sqrt(normal.x * normal.x + normal.z * normal.z);
                        if (r < 1e-4f || nh < 0.8f || (normal.x * dx + normal.z * dz) / (r * nh) < 0.85f)
                            continue;
                        radii.push_back(r);
                        sum_x += vertex->pos[0] - normal.x / nh * r;
                        sum_z += vertex->pos[2] - normal.z / nh * r;
                    }
                    if (radii.size() < 6)
                        break;
                    cx = sum_x / radii.size();
                    cz = sum_z / radii.size();
                }
                if (radii.size() >= 6)
                {
                    std::nth_element(radii.begin(), radii.begin() + radii.size() / 2, radii.end());
                    radius  = radii[radii.size() / 2];
                    support = static_cast<uint32_t>(radii.size());
                }
                SP_LOG_INFO("breakable_trees: trunk of %s/%u measures %.0f cm across from %zu of %zu bark vertices", render->GetMeshName().c_str(), render->GetSubMeshIndex(), radius * 200.0f, radii.size(), band.size());
            }
        }
        trunk_radius_cache[key] = { radius, support };
        return trunk_radius_cache[key];
    }

    inline float measure_trunk_radius(Render* render)
    {
        return measure_trunk_section(render).radius;
    }

    inline bool is_trunk_part(Render* render)
    {
        std::string name = render->GetMeshName();
        if (Material* material = render->GetMaterial())
            name += material->GetObjectName();
        return name.find("bark") != std::string::npos || name.find("trunk") != std::string::npos || name.find("stem") != std::string::npos;
    }

    // leaf cards around a low crown face outwards like bark, they never carry the load
    inline bool is_canopy_part(Render* render)
    {
        std::string name = render->GetMeshName();
        if (Material* material = render->GetMaterial())
            name += material->GetObjectName();
        for (const char* word : { "foliage", "leaf", "leaves", "needle", "branch", "twig" })
        {
            if (name.find(word) != std::string::npos)
                return true;
        }
        return false;
    }

    inline void index_single(Stand& stand)
    {
        Entity* root = World::GetEntityById(stand.root_id);
        BoundingBox bounds;
        if (!root || !stand_bounds(stand, bounds))
            return;

        Tree tree;
        tree.base   = Vector3(root->GetPosition().x, std::min(root->GetPosition().y, bounds.GetMin().y + 0.2f), root->GetPosition().z);
        tree.height = bounds.GetMax().y - tree.base.y;

        // the section most bark agrees on, a bark sub mesh can hold only branches that brush the band
        // models that name no part bark have every part measured
        bool has_collision = false;
        bool any_named     = false;
        for_each_part(stand, [&](Entity* part, Render* render)
        {
            has_collision |= part->GetComponent<Physics>() != nullptr;
            any_named     |= is_trunk_part(render);
        });
        uint32_t best_support = 0;
        for_each_part(stand, [&](Entity* part, Render* render)
        {
            if (any_named ? !is_trunk_part(render) : is_canopy_part(render))
                return;
            const TrunkSection section = measure_trunk_section(render);
            if (section.radius <= 0.0f || section.support <= best_support)
                return;
            best_support = section.support;
            const Vector3 scale = part->GetScale();
            tree.diameter = 2.0f * section.radius * 0.5f * (std::abs(scale.x) + std::abs(scale.z));
        });
        if (tree.diameter <= 0.0f)
            tree.diameter = tree.height / 70.0f;
        tree.diameter = std::clamp(tree.diameter, 0.02f, 2.0f);

        const int32_t x = static_cast<int32_t>(std::floor(tree.base.x / index_cell));
        const int32_t z = static_cast<int32_t>(std::floor(tree.base.z / index_cell));
        stand.grid[cell_key(x, z)].push_back(0);
        stand.trees   = { tree };
        stand.indexed = true;
        if (!has_collision)
            collider_create(stand, root);
        SP_LOG_INFO("breakable_trees: %s stands %.1f m with a %.0f cm trunk%s", root->GetObjectName().c_str(), tree.height, tree.diameter * 100.0f, has_collision ? "" : ", trunk collider added");
    }

    inline void Index(Stand& stand)
    {
        if (stand.single)
        {
            index_single(stand);
            return;
        }
        Entity* part = World::GetEntityById(stand.part_ids.front());
        Render* render = part ? part->GetComponent<Render>() : nullptr;
        if (!render || !render->HasInstancing())
            return;

        const uint32_t count = render->GetInstanceCount();
        std::vector<Tree> trees(count);
        const Matrix world = part->GetMatrix();
        for (uint32_t i = 0; i < count; ++i)
            trees[i].base = render->GetInstancePosition(i, world);

        // the tallest part decides the height, the canopy sits above the trunk
        // instance bounds exist once the render has prepared its groups, retry until then
        bool measured = false;
        Render* trunk = nullptr;
        for_each_part(stand, [&](Entity* part_entity, Render* part_render)
        {
            if (part_render->GetInstanceCount() != count || part_render->GetInstanceBoundsGroups().empty())
                return;
            measured = true;
            for (uint32_t i = 0; i < count; ++i)
                trees[i].height = std::max(trees[i].height, part_render->GetInstanceBounds(i).GetMax().y - trees[i].base.y);

            // the part with collision is the trunk, layers without collision name it
            if (part_entity->GetComponent<Physics>() || (is_trunk_part(part_render) && !trunk))
                trunk = part_render;
        });
        if (!measured)
            return;

        const float mesh_radius = trunk ? measure_trunk_radius(trunk) : 0.0f;
        for (uint32_t i = 0; i < count; ++i)
        {
            Tree& tree = trees[i];
            if (mesh_radius > 0.0f)
            {
                const Vector3 scale = trunk->GetInstanceData(i).GetMatrix().GetScale();
                tree.diameter = 2.0f * mesh_radius * 0.5f * (std::abs(scale.x) + std::abs(scale.z));
            }
            else
            {
                // no trunk mesh to read, a stand grown tree is about seventy times taller than it is thick
                tree.diameter = tree.height / 70.0f;
            }
            tree.diameter = std::clamp(tree.diameter, 0.02f, 2.0f);

            const int32_t x = static_cast<int32_t>(std::floor(tree.base.x / index_cell));
            const int32_t z = static_cast<int32_t>(std::floor(tree.base.z / index_cell));
            stand.grid[cell_key(x, z)].push_back(i);
        }
        stand.trees   = std::move(trees);
        stand.indexed = true;
    }

    // signed distance from a point to the horizontal footprint of a body
    inline float footprint_distance(const SurfaceInteractor& shape, const Vector3& center, const Vector3& point)
    {
        Vector3 right(shape.right.x, 0.0f, shape.right.z);
        if (right.LengthSquared() < 0.01f)
            right = Vector3::Right;
        right.Normalize();
        const Vector3 forward(-right.z, 0.0f, right.x);
        const Vector3 delta(point.x - center.x, 0.0f, point.z - center.z);
        const float qx = std::abs(Vector3::Dot(delta, right)) - shape.extents.x;
        const float qz = std::abs(Vector3::Dot(delta, forward)) - shape.extents.z;
        const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qz, 0.0f) * std::max(qz, 0.0f));
        return outside + std::min(std::max(qx, qz), 0.0f) - shape.radius;
    }

    inline void apply(const Motion& motion)
    {
        auto it = stands.find(motion.stand_id);
        if (it == stands.end())
            return;
        if (it->second.single)
        {
            if (Entity* root = World::GetEntityById(it->second.root_id))
            {
                const Quaternion rotation = Quaternion::FromAxisAngle(motion.axis, motion.angle);
                root->SetPositionAndRotation(motion.pivot + rotation * (motion.original_position - motion.pivot), rotation * motion.original_rotation);
            }
            return;
        }
        const Matrix about_root = Matrix::CreateTranslation(-motion.pivot) * Matrix::CreateRotation(Quaternion::FromAxisAngle(motion.axis, motion.angle)) * Matrix::CreateTranslation(motion.pivot);
        for_each_part(it->second, [&](Entity* part, Render* render)
        {
            if (motion.instance >= render->GetInstanceCount())
                return;
            const Matrix world = part->GetMatrix();
            Instance instance;
            instance.SetMatrix(motion.original.GetMatrix() * world * about_root * world.Inverted());
            render->SetInstance(motion.instance, instance);
        });
    }

    inline void restore(const Motion& motion)
    {
        auto it = stands.find(motion.stand_id);
        if (it == stands.end())
            return;
        if (it->second.single)
        {
            if (Entity* root = World::GetEntityById(it->second.root_id))
                root->SetPositionAndRotation(motion.original_position, motion.original_rotation);
            if (motion.falling)
                collider_enable(it->second, true);
        }
        for_each_part(it->second, [&](Entity* part, Render* render)
        {
            if (!it->second.single && motion.instance < render->GetInstanceCount())
                render->SetInstance(motion.instance, motion.original);
            if (motion.falling)
            {
                if (Physics* physics = part->GetComponent<Physics>())
                    physics->SetInstanceSuppressed(motion.instance, false);
            }
        });
        if (motion.falling)
            it->second.fallen.erase(motion.instance);
    }

    inline Motion* find_motion(uint64_t stand_id, uint32_t instance)
    {
        for (Motion& motion : motions)
        {
            if (motion.stand_id == stand_id && motion.instance == instance)
                return &motion;
        }
        return nullptr;
    }

    inline Vector3 topple_axis(const Vector3& direction)
    {
        // the axis sign is whichever tips the crown towards the push
        Vector3 axis = Vector3::Cross(Vector3::Up, direction).Normalized();
        if (Vector3::Dot(Quaternion::FromAxisAngle(axis, 0.3f) * Vector3::Up, direction) < 0.0f)
            axis = -axis;
        return axis;
    }

    inline void slow_body(const vegetation_interaction::Body& body, const Vector3& direction, float delta_v)
    {
        if (!body.car || delta_v <= 0.0f)
            return;
        Entity* root = body.car->GetRootEntity();
        Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
        auto* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        if (physx::PxRigidDynamic* chassis = simulation ? simulation->get_body() : nullptr)
            chassis->addForce(physx::PxVec3(-direction.x * delta_v, 0.0f, -direction.z * delta_v), physx::PxForceMode::eVELOCITY_CHANGE);
    }

    // the impact, a spring of the body in series with the trunk and the root plate
    inline void capture_original(const Stand& stand, Motion& motion)
    {
        if (stand.single)
        {
            if (Entity* root = World::GetEntityById(stand.root_id))
            {
                motion.original_position = root->GetPosition();
                motion.original_rotation = root->GetRotation();
            }
            return;
        }
        Entity* first = World::GetEntityById(stand.part_ids.front());
        if (Render* render = first ? first->GetComponent<Render>() : nullptr)
            motion.original = render->GetInstanceData(motion.instance);
    }

    inline void impact(uint64_t stand_id, Stand& stand, uint32_t instance, const vegetation_interaction::Body& body, const Vector3& direction, float approach)
    {
        if (instance >= tree_count(stand) || instance >= stand.trees.size())
            return;

        const Tree& tree   = stand.trees[instance];
        const Wood& wood   = stand.wood;
        const float scale  = std::max(cvar_strength_scale.GetValue(), 0.01f);
        const float d      = tree.diameter;
        const float height = std::max(tree.height, 1.0f);
        const float mass   = std::max(body.shape.mass, 1.0f);

        // the tree
        const float area        = pi * d * d / 4.0f;
        const float stem_mass   = wood.density * stem_form * area * height;
        const float tree_mass   = stem_mass * crown_mass;
        const float inertia     = tree_mass * height * height / 3.0f; // a pole about its root
        const float stem_moment = wood.strength * scale * pi * d * d * d / 32.0f;
        const float root_moment = wood.anchorage * scale * stem_mass;
        // green wood folds through a plastic hinge before it parts, a root plate rotates far before it tips,
        // the tree fails along whichever path takes less work
        const float stem_work   = stem_hinge * stem_moment;
        const float root_work   = root_tip * root_moment;
        const bool uproots      = root_work < stem_work;
        const float fail_moment = uproots ? root_moment : stem_moment;

        // where the body meets the trunk, a bumper or a hip, the lever of the load about the root
        const float lever         = std::clamp(body.shape.center.y - tree.base.y, 0.25f, std::max(0.25f, std::min(1.5f, height * 0.5f)));
        const float second_moment = pi * d * d * d * d / 64.0f;
        const float stem_spring   = 3.0f * wood.stiffness * second_moment / (lever * lever * lever);
        const float root_spring   = root_moment / root_yield / (lever * lever);
        const float tree_spring   = series(stem_spring, root_spring);
        const float spring        = series(tree_spring, body.stiffness);

        // peak force of a mass on a spring arriving at speed v, the body folds once it reaches its own limit
        const float fail_force = fail_moment / lever;
        float force            = approach * std::sqrt(spring * mass);
        float energy           = 0.5f * mass * approach * approach;
        float crushed          = 0.0f;
        if (force > body.force_max)
        {
            force = body.force_max;
            // folding at less than the tree needs, the crumple zone goes first and whatever is left arrives on the hard core
            const float crush_energy = body.force_max * body.crush_depth;
            if (body.force_max < fail_force && body.core_stiffness > 0.0f && energy > crush_energy)
            {
                const float residual = std::sqrt(approach * approach - 2.0f * crush_energy / mass);
                force    = std::max(force, residual * std::sqrt(series(tree_spring, body.core_stiffness) * mass));
                energy  -= crush_energy;
                crushed  = crush_energy;
            }
        }
        stand.hit_time[instance] = clock;

        // the force has to reach the failure moment and the body has to carry the work to get there
        const float stored   = fail_force * fail_force / (2.0f * std::max(spring, 1.0f));
        const float absorbed = stored + (uproots ? root_work : stem_work);
        if (force * lever < fail_moment || energy < absorbed)
        {
            // it holds, the body stops against it and the impulse sets the crown swinging
            if (body.car)
            {
                SP_LOG_INFO("breakable_trees: a %.1f m tree, trunk %.0f cm, held a %.0f kg car at %.1f m/s, %.0f kN of %.0f kN, %.0f kJ of %.0f kJ",
                    height, d * 100.0f, mass, approach, force * 0.001f, fail_force * 0.001f, energy * 0.001f, absorbed * 0.001f);
            }
            const float frequency = std::clamp(3.5f * d * 100.0f / (height * height), 0.2f, 4.0f);
            const float omega     = 2.0f * pi * frequency;
            const float amplitude = std::min(mass * approach * lever / inertia / omega, 0.25f);
            if (amplitude < 0.002f)
                return;
            Motion* existing = find_motion(stand_id, instance);
            if (existing && existing->falling)
                return;
            Motion sway;
            if (existing)
            {
                sway = *existing;
            }
            else
            {
                sway.stand_id = stand_id;
                sway.instance = instance;
                capture_original(stand, sway);
                sway.pivot    = tree.base;
                sway.height   = height;
            }
            sway.falling   = false;
            sway.axis      = topple_axis(direction);
            sway.amplitude = std::max(amplitude, existing ? existing->amplitude * std::exp(-sway_damping * existing->omega * existing->age) : 0.0f);
            sway.omega     = omega;
            sway.age       = 0.0f;
            if (existing)
                *existing = sway;
            else
                motions.push_back(sway);
            return;
        }

        // it gives, the body keeps what the break did not take
        const float speed_after = std::sqrt(std::max(approach * approach - 2.0f * (absorbed + crushed) / mass, 0.0f));
        const float delta_v     = approach - speed_after;

        Motion motion;
        if (Motion* existing = find_motion(stand_id, instance))
        {
            motion = *existing;
            motions.erase(motions.begin() + (existing - motions.data()));
        }
        else
        {
            motion.instance = instance;
            capture_original(stand, motion);
        }
        motion.stand_id = stand_id;
        motion.instance = instance;
        motion.pivot    = tree.base;
        motion.height   = height;
        motion.falling  = true;
        motion.axis     = topple_axis(direction);
        // a torn root plate already leans, a snapped stem starts upright
        motion.angle            = uproots ? root_yield : 0.02f;
        motion.angular_velocity = std::clamp(mass * delta_v * lever / inertia, 0.05f, 3.0f);
        motion.age              = 0.0f;

        // lie on the ground where the crown lands, downhill trees tip past horizontal
        const float reach = height * 0.6f;
        const Vector3 probe = tree.base + direction * reach + Vector3(0.0f, height, 0.0f);
        Vector3 hit;
        float drop = 0.0f;
        if (PhysicsWorld::RaycastStatic(probe, Vector3::Down, height * 3.0f, hit) && hit.y < tree.base.y + 1.5f)
            drop = tree.base.y - hit.y;
        motion.angle_rest = std::clamp(1.5708f + std::atan2(drop, reach), 1.22f, 1.92f);

        stand.fallen.insert(instance);
        for_each_part(stand, [&](Entity* part, Render*)
        {
            if (Physics* physics = part->GetComponent<Physics>())
                physics->SetInstanceSuppressed(instance, true);
        });
        collider_enable(stand, false);
        slow_body(body, direction, delta_v);

        SP_LOG_INFO("breakable_trees: %s a %.1f m tree, trunk %.0f cm, %.0f kg, hit at %.1f m/s by %.0f kg with %.0f kN against %.0f kN%s, %.1f m/s left",
            uproots ? "uprooted" : "snapped", height, d * 100.0f, tree_mass, approach, mass, force * 0.001f, fail_force * 0.001f, crushed > 0.0f ? " after crushing the front" : "", speed_after);

        motions.push_back(motion);
        apply(motions.back());

        // too many on the ground, the oldest resting one stands up
        uint32_t fallen_count = 0;
        for (const Motion& m : motions)
            fallen_count += m.falling ? 1 : 0;
        if (fallen_count > fallen_max)
        {
            auto oldest = std::max_element(motions.begin(), motions.end(), [](const Motion& a, const Motion& b)
            {
                return (a.falling && a.resting ? a.age : -1.0f) < (b.falling && b.resting ? b.age : -1.0f);
            });
            if (oldest->falling && oldest->resting)
            {
                restore(*oldest);
                motions.erase(oldest);
            }
        }
    }

    inline void detect(const vegetation_interaction::Body& body)
    {
        const SurfaceInteractor& shape = body.shape;
        Vector3 velocity(shape.velocity.x, 0.0f, shape.velocity.z);
        const float speed = velocity.Length();
        if (speed < 0.3f || shape.mass <= 0.0f || body.stiffness <= 0.0f)
            return;

        // the body now and a moment ahead, as one horizontal box
        const Vector3 ahead   = shape.center + velocity * lookahead;
        const float reach     = std::sqrt(shape.extents.x * shape.extents.x + shape.extents.z * shape.extents.z) + shape.radius + 1.0f;
        const Vector3 sweep_min(std::min(shape.center.x, ahead.x) - reach, shape.center.y - shape.extents.y - shape.radius - 1.0f, std::min(shape.center.z, ahead.z) - reach);
        const Vector3 sweep_max(std::max(shape.center.x, ahead.x) + reach, shape.center.y + shape.extents.y + shape.radius + 1.0f, std::max(shape.center.z, ahead.z) + reach);
        const BoundingBox sweep(sweep_min, sweep_max);
        const float bottom = shape.center.y - shape.extents.y - shape.radius;
        const float top    = shape.center.y + shape.extents.y + shape.radius;

        for (auto& [stand_id, stand] : stands)
        {
            BoundingBox bounds;
            if (!stand_bounds(stand, bounds) || bounds.Intersects(sweep) == Intersection::Outside)
                continue;
            if (!stand.indexed)
                Index(stand);
            if (stand.trees.empty())
                continue;

            const int32_t x0 = static_cast<int32_t>(std::floor(sweep_min.x / index_cell));
            const int32_t x1 = static_cast<int32_t>(std::floor(sweep_max.x / index_cell));
            const int32_t z0 = static_cast<int32_t>(std::floor(sweep_min.z / index_cell));
            const int32_t z1 = static_cast<int32_t>(std::floor(sweep_max.z / index_cell));
            for (int32_t x = x0; x <= x1; ++x)
            for (int32_t z = z0; z <= z1; ++z)
            {
                auto cell = stand.grid.find(cell_key(x, z));
                if (cell == stand.grid.end())
                    continue;
                for (uint32_t i : cell->second)
                {
                    if (stand.fallen.count(i))
                        continue;
                    // one contact per approach, the body is stopped or through after it
                    auto last = stand.hit_time.find(i);
                    if (last != stand.hit_time.end() && clock - last->second < 0.5f)
                        continue;
                    const Tree& tree = stand.trees[i];
                    if (tree.height < 1.0f || bottom > tree.base.y + std::min(tree.height, 2.0f) || top < tree.base.y - 0.5f)
                        continue;

                    const float distance = std::min(footprint_distance(shape, shape.center, tree.base), footprint_distance(shape, ahead, tree.base));
                    if (distance > tree.diameter * 0.5f)
                        continue;

                    Vector3 to_trunk(tree.base.x - shape.center.x, 0.0f, tree.base.z - shape.center.z);
                    if (to_trunk.LengthSquared() < 1e-4f)
                        to_trunk = velocity;
                    to_trunk.Normalize();
                    const float approach = Vector3::Dot(velocity, to_trunk);
                    if (approach < 0.3f)
                        continue;

                    const Vector3 direction = (velocity / speed * 0.7f + to_trunk * 0.3f).Normalized();
                    impact(stand_id, stand, i, body, direction, approach);
                }
            }
        }
    }

    inline void Tick(float dt)
    {
        if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused) || World::IsLoadingFromFile() || dt <= 0.0f)
            return;
        clock += dt;

        scan_timer -= dt;
        if (scan_timer <= 0.0f || World::GetEntities().size() != scanned_entity_count)
        {
            if (World::GetEntities().size() != scanned_entity_count || stands.empty())
                Scan();
            scan_timer = 2.0f;
            // placed trees are few and need their trunk collider before anything arrives
            for (auto& [id, stand] : stands)
            {
                if (stand.single && !stand.indexed)
                    Index(stand);
            }
        }

        for (const vegetation_interaction::Body& body : vegetation_interaction::bodies)
            detect(body);

        Camera* camera = World::GetCamera();
        const Vector3 viewer = camera ? camera->GetEntity()->GetPosition() : Vector3::Zero;
        const float respawn_distance = std::max(cvar_respawn_distance.GetValue(), 20.0f);
        for (size_t i = 0; i < motions.size();)
        {
            Motion& motion = motions[i];
            motion.age += dt;

            if (!motion.falling)
            {
                // a damped oscillator, released once it has settled
                const float decay = sway_damping * motion.omega;
                if (motion.age * decay > 5.0f)
                {
                    restore(motion);
                    motions.erase(motions.begin() + i);
                    continue;
                }
                const float omega_damped = motion.omega * std::sqrt(1.0f - sway_damping * sway_damping);
                motion.angle = motion.amplitude * std::exp(-decay * motion.age) * std::sin(omega_damped * motion.age);
                apply(motion);
                ++i;
                continue;
            }

            // topple, a rigid pole about its root, then a couple of small bounces on the ground
            if (!motion.resting)
            {
                const float acceleration = 1.5f * gravity / motion.height * std::sin(motion.angle);
                motion.angular_velocity += acceleration * dt;
                motion.angle            += motion.angular_velocity * dt;
                if (motion.angle >= motion.angle_rest)
                {
                    motion.angle = motion.angle_rest;
                    if (motion.bounces < 2 && motion.angular_velocity > 0.5f)
                    {
                        motion.angular_velocity *= -0.2f;
                        motion.bounces++;
                    }
                    else
                    {
                        motion.angular_velocity = 0.0f;
                        motion.resting          = true;
                    }
                }
                apply(motion);
            }

            // nobody near and nobody looking, it stands where it grew
            const float distance = Vector3::Distance(viewer, motion.pivot);
            const BoundingBox crown(motion.pivot - Vector3(motion.height, 0.5f, motion.height), motion.pivot + Vector3(motion.height, motion.height, motion.height));
            const bool seen = camera && camera->IsInViewFrustum(crown);
            if (motion.resting && distance > respawn_distance && (!seen || distance > respawn_distance * 2.0f))
            {
                restore(motion);
                motions.erase(motions.begin() + i);
                continue;
            }
            ++i;
        }
    }

    // play stopped, every tree stands where the world file put it
    inline void RestoreAll()
    {
        for (const Motion& motion : motions)
            restore(motion);
        motions.clear();
        // edited trees move between plays, the next play measures them again
        for (auto& [id, stand] : stands)
        {
            stand.hit_time.clear();
            collider_release(stand);
            stand.grid.clear();
            stand.trees.clear();
            stand.fallen.clear();
            stand.indexed = false;
        }
    }

    // the world is going away, nothing to restore
    inline void Clear()
    {
        motions.clear();
        for (auto& [id, stand] : stands)
            collider_release(stand);
        stands.clear();
        if (bark)
        {
            std::lock_guard<std::recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
            bark->release();
            bark = nullptr;
        }
        trunk_radius_cache.clear();
        scanned_entity_count = 0;
        scan_timer           = 0.0f;
    }
}
