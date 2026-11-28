/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================
#include "pch.h"
#include <sol/sol.hpp>
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../math/Quaternion.h"
//===========================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    void register_math_rotation(sol::state_view state)
    {
        visual::usertype<Vector4>(state, "Vector4",
            sol::call_constructor,
            sol::constructors<Vector4(), Vector4(const Vector4&), Vector4(float, float, float, float)>(),

            "x", &Vector4::x,
            "y", &Vector4::y,
            "z", &Vector4::z,
            "w", &Vector4::w,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS + RHS; },
                [](const Vector4& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS - RHS; },
                [](const Vector4& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS * RHS; },
                [](const Vector4& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS / RHS; },
                [](const Vector4& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector4& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector4& LHS, const Vector4& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector4& V)
            {
                return "Vector4(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ", " + std::to_string(V.z) + ", " + std::to_string(V.w) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector4& V) { return 4; },

            // Index access
            sol::meta_function::index, [](const Vector4& V, int index) -> float {
                if (index == 1)
                {
                    return V.x;
                }
                if (index == 2)
                {
                    return V.y;
                }
                if (index == 3)
                {
                    return V.z;
                }
                if (index == 4)
                {
                    return V.w;
                }
                throw std::out_of_range("Vector4 index out of range (1-4)");
            },

            sol::meta_function::new_index, [](Vector4& V, int index, float value) {
                if (index == 1)
                {
                    V.x = value;
                }
                else if (index == 2)
                {
                    V.y = value;
                }
                else if (index == 3)
                {
                    V.z = value;
                }
                else if (index == 4)
                {
                    V.w = value;
                }
                else
                {
                    throw std::out_of_range("Vector4 index out of range (1-4)");
                }
            },

            // Utility methods
            "Length", [](const Vector4& V) { return V.Length(); },
            "LengthSquared", [](const Vector4& V) { return V.LengthSquared(); },
            "Normalize", [](Vector4& V) { return V.Normalize(); },
            "Normalized", [](const Vector4& V) { return V.Normalized(); },
            "Distance", [](const Vector4& V, const Vector4& Other) { return Vector4::Distance(V, Other); },
            "DistanceSquared", [](const Vector4& V, const Vector4& Other) { return Vector4::DistanceSquared(V, Other); }
        );

        visual::usertype<Quaternion>(state, "Quaternion",
            sol::call_constructor,
            sol::constructors<Quaternion(), Quaternion(float, float, float, float)>(),

            "x", &Quaternion::x,
            "y", &Quaternion::y,
            "z", &Quaternion::z,
            "w", &Quaternion::w,

            // a * b applies b first, a * v rotates the vector
            sol::meta_function::multiplication, sol::overload(
                [](const Quaternion& a, const Quaternion& b) { return a * b; },
                [](const Quaternion& q, const Vector3& v) { return q * v; }
            ),

            "FromEulerAngles", [](float pitch, float yaw, float roll) { return Quaternion::FromEulerAngles(pitch, yaw, roll); },
            // angle in radians
            "FromAxisAngle", [](const Vector3& axis, float angle) { return Quaternion::FromAxisAngle(axis.Normalized(), angle); },
            "Inverse",       [](const Quaternion& q) { return q.Inverse(); },
            "Normalized",    [](const Quaternion& q) { return q.Normalized(); },
            "FromLookRotation", [](const Vector3& direction, const Vector3& up)
            {
                return Quaternion::FromLookRotation(direction, up);
            },
            "Lerp", [](const Quaternion& a, const Quaternion& b, float t)
            {
                return Quaternion::Lerp(a, b, t);
            },
            "Identity",        sol::var(Quaternion::Identity)
        );
    }
}
