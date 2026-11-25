/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============
#include "pch.h"
#include "Component.h"
#include "../Entity.h"
#include "AudioSource.h"
#include "Camera.h"
#include "Light.h"
#include "Physics.h"
#include "Script.h"
#include "Spline.h"
#include "Terrain.h"
#include "Volume.h"
#include "ParticleSystem.h"
#include "SplineFollower.h"
#include "Water.h"
#include "Navigation.h"
#include "SpawnPoint.h"
#include "Text3D.h"
#include "Animator.h"
#include "Ragdoll.h"
#include "Pedestrians.h"
#include "SkidMarks.h"
#include "Traffic.h"
#include "RaceDriver.h"
#include "RouteDriver.h"
#include "CarReset.h"
#include "CarService.h"
#include "NetworkSync.h"
#include "Destructible.h"
//=========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    Component::Component(Entity* entity)
    {
        m_entity_ptr = entity;
        m_enabled    = true;
    }

    std::string Component::TypeToString(ComponentType type)
    {
        switch (type)
        {
            #define X(type, str) case ComponentType::type: return #str;
            SP_COMPONENT_LIST
            #undef X
            default:
                assert(false && "TypeToString: Unknown ComponentType");
                return {};
        }
    }

    ComponentType Component::StringToType(const std::string& name)
    {
        #define X(type, str) if (name == #str) return ComponentType::type;
        SP_COMPONENT_LIST
        #undef X

        SP_LOG_ERROR("Unsupported component in this application: %s", name.c_str());
        return ComponentType::Max;
    }

    void Component::RegisterAttribute(const char* name, const char* type, std::function<std::any()>&& getter, std::function<void(std::any)>&& setter, bool writable)
    {
        Attribute attribute;
        attribute.writable = writable;
        attribute.name   = name;
        attribute.type   = type;
        attribute.getter = std::move(getter);
        attribute.setter = std::move(setter);
        m_attributes.emplace_back(std::move(attribute));
    }

    void Component::SetAttributes(const std::vector<Attribute>& attributes)
    {
        SP_ASSERT(m_attributes.size() == attributes.size());
        for (const Attribute& destination : m_attributes)
        {
            const auto source = std::find_if(attributes.begin(), attributes.end(), [&](const Attribute& value) { return value.name == destination.name; });
            SP_ASSERT(source != attributes.end() && source->type == destination.type);
            destination.setter(source->getter());
        }
        if (m_entity_ptr) m_entity_ptr->RefreshPreTickGate();
    }

    // auto-generated from SP_COMPONENT_LIST - no manual registration needed
    #define X(type, str) static_assert(is_base_of<Component, type>::value, "Provided type does not implement IComponent");
    SP_COMPONENT_LIST
    #undef X
}
