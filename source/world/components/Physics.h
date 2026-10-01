/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include "Component.h"

#include <cstdint>
#include <vector>
#include <memory>
#include "../../math/Vector3.h"
#include "../../math/Quaternion.h"
#include "../../rhi/RHI_Vertex.h"
//=================================

namespace sol
{
    class state_view;
}

namespace spartan
{
    class Entity;
    class Mesh;
    class PhysicsWorld;
    namespace math { class Quaternion; }

    enum class PhysicsForce
    {
        Constant,
        Impulse
    };

    enum class BodyType
    {
        Box,
        Sphere,
        Plane,
        Capsule,
        Mesh,
        MeshConvex, // compound shape built from convex hulls of entity hierarchy meshes
        Controller,
        Custom,     // value 7 preserves existing external-body world files
        Cloth,      // deformable surface simulated via verlet integration
        Heightfield,// terrain grid, exact and far cheaper than a cooked mesh of the same surface
        Max
    };

    struct PhysicsSettings
    {
        void Validate();
        float mass = 1.0f, friction = 0.4f, friction_rolling = 0.4f, restitution = 0.2f;
        bool is_static = true, is_kinematic = false;
        bool use_convex_hull = false, distance_streaming = true;
        math::Vector3 position_lock, rotation_lock, center_of_mass;
        BodyType body_type = BodyType::Max;
        float cloth_stiffness = 0.9f, cloth_damping = 0.01f;
        uint32_t cloth_iterations = 8;
        bool cloth_wind_enabled = true;
        math::Vector3 cloth_pin_direction = math::Vector3::Up;
    };

    // An optional force-model body. The implementation owns its actors; Physics borrows
    // the primary actor for ordinary queries, forces and editor interaction.
    struct PhysicsBody
    {
        virtual ~PhysicsBody() = default;
        virtual void* Create() = 0;
        virtual void Remove() = 0;
        virtual void Tick(bool playing) = 0;
        virtual void ShiftOrigin(const math::Vector3& shift) = 0;
        virtual bool SetTransform(const math::Vector3& position, const math::Quaternion& rotation, bool reset_simulation) = 0;
    };

    class Physics : public Component
    {
    public:
        bool StartsEarly() const override { return true; }
        Physics(Entity* entity);
        ~Physics();

        // component
        void Initialize() override;
        void Remove() override;
        // discard the current actors and build them again from the component's current state
        void Rebuild() { Create(); }
        bool PrepareWorld() override;
        void PreTick() override;
        void Tick() override;
        void CopyFrom(const Component& source) override;
        PhysicsSettings GetSettings() const;
        void ApplySettings(const PhysicsSettings& settings);
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // static cleanup (call before physics world shutdown)
        static void Shutdown();

        // fft water buoyancy, runs once per fixed physics step for every dynamic body
        static void TickBuoyancy();
        static void ShiftOrigin(const math::Vector3& shift);

        // mass
        constexpr static inline float mass_from_volume = FLT_MAX;
        float GetMass() const { return m_mass; }
        void SetMass(float mass);

        // friction
        float GetFriction() const { return m_friction; }
        void SetFriction(float friction);

        // angular friction
        float GetFrictionRolling() const { return m_friction_rolling; }
        void SetFrictionRolling(float friction_rolling);

        // restitution
        float GetRestitution() const { return m_restitution; }
        void SetRestitution(float restitution);

        // forces
        void SetLinearVelocity(const math::Vector3& velocity) const;
        math::Vector3 GetLinearVelocity() const;
        math::Vector3 GetAngularVelocity() const;
        void SetAngularVelocity(const math::Vector3& velocity) const;
        void ApplyForce(const math::Vector3& force, PhysicsForce mode) const;

        // position lock
        void SetPositionLock(bool lock);
        void SetPositionLock(const math::Vector3& lock);
        math::Vector3 GetPositionLock() const { return m_position_lock; }

        // rotation lock
        void SetRotationLock(bool lock);
        void SetRotationLock(const math::Vector3& lock);
        math::Vector3 GetRotationLock() const { return m_rotation_lock; }

        // center of mass
        void SetCenterOfMass(const math::Vector3& center_of_mass);
        const math::Vector3& GetCenterOfMass() const { return m_center_of_mass; }

        // body type
        BodyType GetBodyType() const { return m_body_type; }
        void SetBodyType(BodyType type);
        BodyType DetectBodyType();

        // a BodyType::Mesh body cooks a triangle mesh when static and a convex hull when dynamic,
        // this forces the hull for statics too, which is what scattered props want, a rock or a trunk
        // is close enough to convex that the exact silhouette buys nothing and costs per contact
        bool GetUseConvexHull() const { return m_use_convex_hull; }
        void SetUseConvexHull(bool enabled);

        // the render rewrote its instance list, the per instance actors are rebuilt on the next tick
        void OnInstancesChanged();
        bool NeedsRunningPreTick() const
        {
            return !m_is_static || m_needs_creation || m_instances_dirty ||
                m_body_type == BodyType::Controller || m_body_type == BodyType::Custom || m_body_type == BodyType::Cloth;
        }

        // ground
        bool IsGrounded() const;
        Entity* GetGroundEntity() const;

        // dimensional properties
        float GetCapsuleVolume();
        float GetCapsuleRadius();
        math::Vector3 GetControllerTopLocal() const;

        // static
        bool IsStatic() const { return m_is_static; }
        void SetStatic(bool is_static);

        // kinematic
        bool IsKinematic() const { return m_is_kinematic; }
        void SetKinematic(bool is_kinematic);

        // enabled (controls whether the physics body processes input/forces)
        bool IsEnabled() const  { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

        // static collision streams in and out around the camera, a race track turns it off so ai cars far from the player keep the road under them
        bool GetDistanceStreaming() const { return m_distance_streaming; }
        void SetDistanceStreaming(bool enabled);

        // misc
        void Move(const math::Vector3& offset);
        void Crouch(const bool crouch);
        void SetBodyTransform(const math::Vector3& position, const math::Quaternion& rotation, bool reset_simulation = true); // teleport physics body

        using BodyFactory = std::unique_ptr<PhysicsBody> (*)(Physics&);
        static void SetBodyFactory(BodyFactory factory);
        PhysicsBody* GetCustomBody() const { return m_custom_body.get(); }
        PhysicsBody& EnsureCustomBody() const;

        // mesh convex compound shape - set the source entity whose hierarchy will be walked
        // to build convex hull shapes from each mesh in the hierarchy
        void SetMeshConvexSourceEntity(Entity* entity);
        Entity* GetMeshConvexSourceEntity() const { return m_mesh_convex_source; }

        // cloth simulation parameters (only applies when body type is Cloth)
        float GetClothStiffness() const            { return m_cloth_stiffness; }
        void SetClothStiffness(float stiffness);
        float GetClothDamping() const              { return m_cloth_damping; }
        void SetClothDamping(float damping);
        uint32_t GetClothIterations() const        { return m_cloth_iterations; }
        void SetClothIterations(uint32_t count);
        bool GetClothWindEnabled() const             { return m_cloth_wind_enabled; }
        void SetClothWindEnabled(bool enabled)       { m_cloth_wind_enabled = enabled; }
        const math::Vector3& GetClothPinDirection() const { return m_cloth_pin_direction; }
        void SetClothPinDirection(const math::Vector3& direction);

    private:
        mutable std::unique_ptr<PhysicsBody> m_custom_body;
        bool m_has_custom_actor = false;

        // tick helpers (broken out for readability)
        void TickController(bool is_playing, float delta_time);
        void LiftControllerAboveTerrain();
        void CreateHeightfield();
        void TickCloth(bool is_playing, float delta_time);
        void TickDynamicBodies(bool is_playing);
        void TickDistanceActivation();
        void SyncStaticPoses();
        void ApplyBuoyancy();
        float ComputeVolume();

        void Create();
        bool CreateController();
        bool CreateConvexCompound();
        bool CreateShapes();
        void CreateBodies();
        void RebuildInstanceActors();
        void CreateCloth();

        float m_mass                   = 1.0f;
        float m_friction               = 0.4f;
        float m_friction_rolling       = 0.4f;
        float m_restitution            = 0.2f;
        bool m_is_static               = true;
        bool m_is_kinematic            = false;
        bool m_enabled                 = true;
        bool m_distance_streaming      = true;
        math::Vector3 m_position_lock  = math::Vector3::Zero;
        math::Vector3 m_rotation_lock  = math::Vector3::Zero;
        math::Vector3 m_center_of_mass = math::Vector3::Zero;
        math::Vector3 m_velocity       = math::Vector3::Zero;
        BodyType m_body_type           = BodyType::Max;
        bool m_use_convex_hull         = false;
        bool m_controller_was_playing  = false; // tracks the edit to play transition of the character controller
        void* m_controller               = nullptr;

        // heightfield shape placement, physx grids start at their corner and store heights as scaled integers
        bool          m_mesh_is_heightfield      = false;
        bool          m_mesh_is_convex           = false;
        math::Vector3 m_heightfield_offset       = math::Vector3::Zero;
        float         m_heightfield_scale_height = 1.0f;
        float         m_heightfield_scale_row    = 1.0f;
        float         m_heightfield_scale_column = 1.0f;

        void* m_material                 = nullptr;
        void* m_mesh                     = nullptr;
        std::vector<void*> m_actors      = { nullptr };
        std::vector<bool> m_actors_active; // tracks which actors are currently in the scene (for distance-based activation)
        uint32_t m_actors_active_count = 0; // how many of the above are in the scene, lets a sleeping entity skip its per instance scan
        math::Vector3 m_activation_camera = math::Vector3::Zero;
        math::Matrix m_activation_world = math::Matrix::Identity;
        float m_activation_slack = 0.0f;
        bool m_activation_valid = false;

        // mesh convex source entity - the entity hierarchy to walk for building compound convex shapes
        Entity* m_mesh_convex_source = nullptr;

        // deferred creation flag for loading (wait until render is available)
        bool m_needs_creation = false;
        // set by the render when its instance list changes, consumed once per frame in PreTick
        bool m_instances_dirty = false;

        // cached scale for detecting editor-time scale changes
        math::Vector3 m_scale_previous = math::Vector3::Zero;
        // cached entity matrix, a static pose is only rewritten when the entity actually moved
        math::Matrix m_transform_previous = math::Matrix::Identity;

        void UpdateShapeGeometry();

        // interpolation state for smooth rendering between fixed physics timesteps
        math::Vector3 m_prev_position     = math::Vector3::Zero; // position at previous physics step
        math::Quaternion m_prev_rotation;                        // rotation at previous physics step
        math::Vector3 m_current_position  = math::Vector3::Zero; // position at current physics step
        math::Quaternion m_current_rotation;                     // rotation at current physics step
        bool m_interpolation_initialized  = false;               // flag to track first-frame initialization

        // cloth simulation state
        struct ClothParticle
        {
            math::Vector3 position;
            math::Vector3 previous_position;
            float inverse_mass = 1.0f; // 0 = pinned
        };

        struct ClothConstraint
        {
            uint32_t index_a = 0;
            uint32_t index_b = 0;
            float rest_length = 0.0f;
        };

        std::vector<ClothParticle> m_cloth_particles;
        std::vector<ClothConstraint> m_cloth_constraints;
        std::vector<uint32_t> m_cloth_indices;           // triangle indices for normal recalculation
        std::vector<RHI_Vertex_PosTexNorTan> m_cloth_base_vertices; // cached original vertices (for tex/tan preservation)
        std::vector<uint32_t> m_cloth_weld_map;          // maps each vertex to its canonical (lowest-index coincident) vertex
        std::shared_ptr<Mesh> m_cloth_mesh;
        uint32_t m_cloth_global_vertex_offset = 0;       // offset into the global geometry buffer
        uint32_t m_cloth_vertex_count         = 0;
        float m_cloth_stiffness               = 0.9f;    // constraint stiffness (0-1)
        float m_cloth_damping                 = 0.01f;   // velocity damping (0-1)
        uint32_t m_cloth_iterations           = 8;       // constraint solver iterations per step
        bool m_cloth_wind_enabled             = true;
        math::Vector3 m_cloth_pin_direction   = math::Vector3::Up;
    };
}
