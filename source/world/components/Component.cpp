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
#include "SkidMarks.h"
#include "Water.h"
#include "Traffic.h"
#include "Pedestrians.h"
#include "Navigation.h"
#include "SpawnPoint.h"
#include "CarReset.h"
#include "Text3D.h"
#include "Animator.h"
#include "Ragdoll.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
SP_WARNINGS_ON
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

    void Component::SetAttributes(const std::vector<Attribute>& attributes)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_attributes.size()); ++i)
            m_attributes[i].setter(attributes[i].getter());
        if (m_entity_ptr) m_entity_ptr->RefreshPreTickGate();
    }

    sol::reference Component::AsLua(sol::state_view state)
    {
        return sol::nil;
    }

    // auto-generated from SP_COMPONENT_LIST - no manual registration needed
    #define X(type, str) static_assert(is_base_of<Component, type>::value, "Provided type does not implement IComponent");
    SP_COMPONENT_LIST
    #undef X
}
