/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =============
#include "pch.h"
#include "Component.h"
#include "Light.h"
#include "PhysicsBody.h"
#include "Constraint.h"
#include "Camera.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "Terrain.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Component::Component(weak_ptr<Entity> entity)
    {
        m_entity_ptr = entity.lock().get();
        m_enabled    = true;
    }

    template <typename T>
    inline constexpr ComponentType Component::TypeToEnum() { return ComponentType::Max; }

    template<typename T>
    inline constexpr void validate_component_type() { static_assert(is_base_of<Component, T>::value, "Provided type does not implement IComponent"); }

    // explicit template instantiation
    #define REGISTER_COMPONENT(T, enumT) template<> SP_CLASS ComponentType Component::TypeToEnum<T>() { validate_component_type<T>(); return enumT; }

    // to add a new component to the engine, simply register it here
    REGISTER_COMPONENT(AudioListener,   ComponentType::AudioListener)
    REGISTER_COMPONENT(AudioSource,     ComponentType::AudioSource)
    REGISTER_COMPONENT(Camera,          ComponentType::Camera)
    REGISTER_COMPONENT(Constraint,      ComponentType::Constraint)
    REGISTER_COMPONENT(Light,           ComponentType::Light)
    REGISTER_COMPONENT(Renderable,      ComponentType::Renderable)
    REGISTER_COMPONENT(PhysicsBody,     ComponentType::PhysicsBody)
    REGISTER_COMPONENT(Terrain,         ComponentType::Terrain)
 
}
