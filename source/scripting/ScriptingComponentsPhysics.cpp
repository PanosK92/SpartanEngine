/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================================
#include "pch.h"
#include <sol/sol.hpp>
#include "Scripting.h"
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../world/Entity.h"
#include "../world/components/Animator.h"
#include "../world/components/Physics.h"
#include "../world/components/Ragdoll.h"
//===================================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    namespace
    {
        void register_physics(sol::state_view State)
        {
            visual::enumeration<BodyType>(State, "BodyType", {
                {"Box",          BodyType::Box},
                {"Sphere",       BodyType::Sphere},
                {"Plane",        BodyType::Plane},
                {"Capsule",      BodyType::Capsule},
                {"Mesh",         BodyType::Mesh},
                {"MeshConvex",   BodyType::MeshConvex},
                {"Controller",   BodyType::Controller},
                {"Custom",       BodyType::Custom},
                {"Cloth",        BodyType::Cloth},
                {"Heightfield",  BodyType::Heightfield},
                {"Max",          BodyType::Max}
            });

            visual::enumeration<ClothPinMode>(State, "ClothPinMode", {
                {"Edge",          ClothPinMode::Edge},
                {"OppositeEdges", ClothPinMode::OppositeEdges},
                {"Border",        ClothPinMode::Border},
                {"None",          ClothPinMode::None}
            });

            visual::usertype<Physics>(State, "Physics",
                sol::base_classes,              sol::bases<Component>(),
                "GetMass",                      &Physics::GetMass,
                "SetMass",                      &Physics::SetMass,
                "GetFriction",                  &Physics::GetFriction,
                "SetFriction",                  &Physics::SetFriction,
                "GetFrictionRolling",           &Physics::GetFrictionRolling,
                "SetFrictionRolling",           &Physics::SetFrictionRolling,
                "GetRestitution",               &Physics::GetRestitution,
                "SetRestitution",               &Physics::SetRestitution,
                "SetLinearVelocity",            &Physics::SetLinearVelocity,
                "GetLinearVelocity",            &Physics::GetLinearVelocity,
                "SetAngularVelocity",           &Physics::SetAngularVelocity,
                "SetCenterOfMass",              &Physics::SetCenterOfMass,
                "GetCenterOfMass",              &Physics::GetCenterOfMass,
                "GetCapsuleVolume",             &Physics::GetCapsuleVolume,
                "GetCapsuleRadius",             &Physics::GetCapsuleRadius,
                "GetClothStiffness",            &Physics::GetClothStiffness,
                "SetClothStiffness",            &Physics::SetClothStiffness,
                "GetClothDamping",              &Physics::GetClothDamping,
                "SetClothDamping",              &Physics::SetClothDamping,
                "GetClothIterations",           &Physics::GetClothIterations,
                "SetClothIterations",           &Physics::SetClothIterations,
                "GetClothWindEnabled",          &Physics::GetClothWindEnabled,
                "SetClothWindEnabled",          &Physics::SetClothWindEnabled,
                "GetClothBending",              &Physics::GetClothBending,
                "SetClothBending",              &Physics::SetClothBending,
                "GetClothPinDirection",         &Physics::GetClothPinDirection,
                "SetClothPinDirection",         &Physics::SetClothPinDirection,
                "GetClothPinMode",              &Physics::GetClothPinMode,
                "SetClothPinMode",              &Physics::SetClothPinMode,
                "ReleaseCloth",                 &Physics::ReleaseCloth,
                "ResetCloth",                   &Physics::ResetCloth,
                "IsClothReleased",              &Physics::IsClothReleased,
                "IsGrounded",                   &Physics::IsGrounded,
                "GetGroundEntity",              &Physics::GetGroundEntity,
                "GetBodyType",                  &Physics::GetBodyType,
                "SetBodyType",                  &Physics::SetBodyType,
                "IsStatic",                     &Physics::IsStatic,
                "SetStatic",                    &Physics::SetStatic,
                "SetBodyTransform",             [](Physics& physics, const Vector3& position, const Quaternion& rotation) { physics.SetBodyTransform(position, rotation, false); }
            );
        }

        void register_animator(sol::state_view state)
        {
            visual::enumeration<IkLimb>(state, "IkLimb", {
                {"HandLeft",  IkLimb::HandLeft},
                {"HandRight", IkLimb::HandRight},
                {"FootLeft",  IkLimb::FootLeft},
                {"FootRight", IkLimb::FootRight}
            });

            visual::usertype<Animator>(state, "Animator",
                sol::base_classes, sol::bases<Component>(),
                "SetLimbIk",              &Animator::SetLimbIk,
                "SetLimbIkPole",          &Animator::SetLimbIkPole,
                "ClearLimbIk",            &Animator::ClearLimbIk,
                "GetLimbIkWeight",        &Animator::GetLimbIkWeight,
                "GetJointPosition",       &Animator::GetJointPosition,
                "GetJointRotation",       &Animator::GetJointRotation,
                "Play",                   &Animator::Play,
                "Stop",                   &Animator::Stop,
                "Pause",                  &Animator::Pause,
                "Resume",                 &Animator::Resume,
                "IsPlaying",              &Animator::IsPlaying,
                "GetSpeed",               &Animator::GetSpeed,
                "SetSpeed",               &Animator::SetSpeed,
                "GetTime",                &Animator::GetTime,
                "SetTime",                &Animator::SetTime,
                "GetClipDuration",        &Animator::GetClipDuration,
                "GetLoop",                &Animator::GetLoop,
                "SetLoop",                &Animator::SetLoop,
                "GetBlendDuration",       &Animator::GetBlendDuration,
                "SetBlendDuration",       &Animator::SetBlendDuration,
                "GetCurrentClip",         &Animator::GetCurrentClip,
                "GetFootIkEnabled",       &Animator::GetFootIkEnabled,
                "SetFootIkEnabled",       &Animator::SetFootIkEnabled,
                "GetFootIkWeight",        &Animator::GetFootIkWeight,
                "SetFootIkWeight",        &Animator::SetFootIkWeight,
                "SetBoneEntitiesEnabled", &Animator::SetBoneEntitiesEnabled,
                "GetFootIkGroundOffset",  &Animator::GetFootIkGroundOffset,
                "HasFootIkSupportGround", &Animator::HasFootIkSupportGround,
                "GetFootIkSupportGroundY", &Animator::GetFootIkSupportGroundY
            );
        }

        void register_ragdoll(sol::state_view state)
        {
            visual::usertype<Ragdoll>(state, "Ragdoll",
                sol::base_classes,   sol::bases<Component>(),
                "IsDead",            &Ragdoll::IsDead,
                "IsFrozen",          &Ragdoll::IsFrozen,
                "Activate",          &Ragdoll::Activate,
                "Wake",              &Ragdoll::Wake,
                "SetHitBodyEnabled", &Ragdoll::SetHitBodyEnabled,
                "IsHitBodyEnabled",  &Ragdoll::IsHitBodyEnabled
            );
        }
    }

    void register_components_physics(sol::state_view state)
    {
        register_physics(state);
        register_animator(state);
        register_ragdoll(state);
    }
}
