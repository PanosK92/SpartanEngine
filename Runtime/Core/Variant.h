/*
Copyright(c) 2016-2022 Panos Karabelas

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

#pragma once

//= INCLUDES ==================
#include "Runtime/Math/Vector2.h"
#include "Runtime/Math/Vector3.h"
#include "Runtime/Math/Vector4.h"
#include "Runtime/Math/Quaternion.h"
#include "Runtime/Math/Matrix.h"
#include <variant>
#include "SpartanDefinitions.h"
//=============================

//= FORWARD DECLARATIONS =========
namespace Spartan
{
    class Entity;
}
typedef union SDL_Event SDL_Event;
//================================

#define _VARIANT_TYPES                             \
    char,                                          \
    unsigned char,                                 \
    int,                                           \
    uint32_t,                                      \
    bool,                                          \
    float,                                         \
    double,                                        \
    void*,                                         \
    Spartan::Entity*,                              \
    std::shared_ptr<Spartan::Entity>,              \
    std::weak_ptr<Spartan::Entity>,                \
    std::vector<std::weak_ptr<Spartan::Entity>>,   \
    std::vector<std::shared_ptr<Spartan::Entity>>, \
    Spartan::Math::Vector2,                        \
    Spartan::Math::Vector3,                        \
    Spartan::Math::Vector4,                        \
    Spartan::Math::Matrix,                         \
    Spartan::Math::Quaternion,                     \
    SDL_Event*

#define VARIANT_TYPES std::variant<_VARIANT_TYPES>
typedef std::variant<_VARIANT_TYPES, VARIANT_TYPES> StdVariant;

namespace Spartan
{
    class SPARTAN_CLASS Variant
    {
    public:
        // Default
        Variant() = default;
        ~Variant() = default;

        // Copy constructor 1
        Variant(const Variant& var){ m_variant = var.GetVariantRaw(); }
        // Copy constructor 2
        template <class T, class = std::enable_if<!std::is_same<T, Variant>::value>>
        Variant(T value) { m_variant = value; }

        // Assignment operator 1
        Variant& operator =(const Variant& rhs);
        // Assignment operator 2
        template <class T, class = std::enable_if<!std::is_same<T, Variant>::value>>
        Variant& operator =(T rhs) { return m_variant = rhs; }

        const StdVariant& GetVariantRaw() const { return m_variant; }

        template<class T>
        inline const T& Get() const { return std::get<T>(m_variant); }

    private:
        StdVariant m_variant;
    };
}
