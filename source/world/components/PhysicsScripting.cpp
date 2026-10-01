
/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Physics.h"
#include "../Entity.h"
#include <sol/sol.hpp>
using namespace spartan::math;
namespace spartan
{
    void Physics::RegisterForScripting(sol::state_view State)
    {

        State.new_enum("BodyType",
            "Box",          BodyType::Box,
            "Sphere",       BodyType::Sphere,
            "Plane",        BodyType::Plane,
            "Capsule",      BodyType::Capsule,
            "Mesh",         BodyType::Mesh,
            "MeshConvex",   BodyType::MeshConvex,
            "Controller",   BodyType::Controller,
            "Custom",       BodyType::Custom,
            "Cloth",        BodyType::Cloth,
            "Heightfield",  BodyType::Heightfield,
            "Max",          BodyType::Max);


        State.new_usertype<Physics>("Physics",
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

            "IsGrounded",                   &Physics::IsGrounded,
            "GetGroundEntity",              &Physics::GetGroundEntity,
            "GetBodyType",                  &Physics::GetBodyType,
            "SetBodyType",                  &Physics::SetBodyType,
            "IsStatic",                     &Physics::IsStatic,
            "SetStatic",                    &Physics::SetStatic,
            "SetBodyTransform",             [](Physics& physics, const Vector3& position, const Quaternion& rotation) { physics.SetBodyTransform(position, rotation, false); }
            );

    }

}
