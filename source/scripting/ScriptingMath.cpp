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
#include "../math/Quaternion.h"
//===========================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    void register_math(sol::state_view state)
    {
        state.new_enum("Intersection",
            "Outside", Intersection::Outside,
            "Inside",       Intersection::Inside,
            "Intersects",   Intersection::Intersects
            );

        state.new_usertype<BoundingBox>("BoundingBox",
            sol::call_constructor,      sol::constructors<BoundingBox(), BoundingBox(Vector3, Vector3)>(),

            "Intersects",               sol::overload(
                [](const BoundingBox& Self, const Vector3& Point) { return Self.Intersects(Point); },
                [](const BoundingBox& Self, const BoundingBox& Other) { return Self.Intersects(Other); }),

            "Contains",                 &BoundingBox::Contains,
            "Merge",                    &BoundingBox::Merge,
            "GetClosestPoint",          &BoundingBox::GetClosestPoint,
            "GetCenter",                &BoundingBox::GetCenter,
            "GetSize",                  &BoundingBox::GetSize,
            "GetExtents",               &BoundingBox::GetExtents,
            "GetVolume",                &BoundingBox::GetVolume,

            "GetMin",                   &BoundingBox::GetMin,
            "GetMax",                   &BoundingBox::GetMax

            );

        state.new_usertype<Vector2>("Vector2",
            sol::call_constructor,
            sol::constructors<Vector2(), Vector2(const Vector2&), Vector2(int, int), Vector2(float, float)>(),

            "x", &Vector2::x,
            "y", &Vector2::y,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS + RHS; },
                [](const Vector2& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS - RHS; },
                [](const Vector2& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS * RHS; },
                [](const Vector2& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS / RHS; },
                [](const Vector2& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector2& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector2& LHS, const Vector2& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector2& V)
            {
                return "Vector2(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector2& V) { return 2; },

            // Index access
            sol::meta_function::index, [](const Vector2& V, int index) -> float {
                if (index == 1)
                {
                    return V.x;
                }
                if (index == 2)
                {
                    return V.y;
                }
                throw std::out_of_range("Vector2 index out of range (1-2)");
            },

            sol::meta_function::new_index, [](Vector2& V, int index, float value) {
                if (index == 1)
                {
                    V.x = value;
                }
                else if (index == 2)
                {
                    V.y = value;
                }
                else
                {
                    throw std::out_of_range("Vector2 index out of range (1-2)");
                }
            },

            // Utility methods
            "Length", [](const Vector2& V) { return V.Length(); },
            "LengthSquared", [](const Vector2& V) { return V.LengthSquared(); },
            "Normalize", [](Vector2& V) { return V.Normalize(); },
            "Normalized", [](const Vector2& V) { return V.Normalized(); },
            "Distance", [](const Vector2& V, const Vector2& Other) { return Vector2::Distance(V, Other); },
            "DistanceSquared", [](const Vector2& V, const Vector2& Other) { return Vector2::DistanceSquared(V, Other); }
        );


        state.new_usertype<Vector3>("Vector3",
            sol::call_constructor,
            sol::constructors<Vector3(), Vector3(const Vector3&), Vector3(float, float, float)>(),

            "x", &Vector3::x,
            "y", &Vector3::y,
            "z", &Vector3::z,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS + RHS; },
                [](const Vector3& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS - RHS; },
                [](const Vector3& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS * RHS; },
                [](const Vector3& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS / RHS; },
                [](const Vector3& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector3& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector3& LHS, const Vector3& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector3& V)
            {
                return "Vector3(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ", " + std::to_string(V.z) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector3& V) { return 3; },

            // Index access
            sol::meta_function::index, [](const Vector3& V, int index) -> float
            {
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
                throw std::out_of_range("Vector3 index out of range (1-3)");
            },

            sol::meta_function::new_index, [](Vector3& V, int index, float value)
            {
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
                else
                {
                    throw std::out_of_range("Vector3 index out of range (1-3)");
                }
            },

            // Utility methods
            "Length", [](const Vector3& V) { return V.Length(); },
            "LengthSquared", [](const Vector3& V) { return V.LengthSquared(); },
            "Normalize", [](Vector3& V) { return V.Normalize(); },
            "Normalized", [](const Vector3& V) { return V.Normalized(); },
            "Distance", [](const Vector3& V, const Vector3& Other) { return Vector3::Distance(V, Other); },
            "DistanceSquared", [](const Vector3& V, const Vector3& Other) { return Vector3::DistanceSquared(V, Other); }
        );


        state.new_usertype<Vector4>("Vector4",
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

        state.new_usertype<Quaternion>("Quaternion",
            sol::call_constructor,
            sol::constructors<Quaternion()>(),

            "x", &Quaternion::x,
            "y", &Quaternion::y,
            "z", &Quaternion::z,
            "w", &Quaternion::w,

            "FromEulerAngles", [](float pitch, float yaw, float roll) { return Quaternion::FromEulerAngles(pitch, yaw, roll); },
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
