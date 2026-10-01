/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========================
#include <any>
#include "../../logging/Log.h"
#include "../../core/PooledObject.h"
#include <vector>
#include <functional>
#include <string>
#include <sol/forward.hpp>
#include "../../core/SpartanObject.h"
//===================================

namespace pugi
{
    class xml_node;
}

#ifdef SP_GAME
#include "../../game/GameComponents.h"
#else
#define SP_GAME_COMPONENT_LIST
#endif

namespace spartan
{
    class Entity;
    class FileStream;

#define SP_ENGINE_COMPONENT_LIST \
    X(AudioSource, audio_source) \
    X(Camera, camera) \
    X(Light, light) \
    X(Physics, physics) \
    X(Render, render) \
    X(Spline, spline) \
    X(SplineFollower, spline_follower) \
    X(Terrain, terrain) \
    X(Volume, volume) \
    X(Script, script) \
    X(ParticleSystem, particle_system) \
    X(Water, water) \
    X(SpawnPoint, spawn_point) \
    X(Text3D, text_3d) \
    X(Animator, animator) \
    X(Ragdoll, ragdoll) \
    X(Navigation, navigation)

#define SP_COMPONENT_LIST SP_ENGINE_COMPONENT_LIST SP_GAME_COMPONENT_LIST

    enum class ComponentType : uint32_t
    {
        #define X(type, str) type,
        SP_COMPONENT_LIST
        #undef X
        Max
    };

    struct Attribute
    {
        std::string name;
        std::string type;
        bool writable = false; // external edits must use a setter that maintains invariants
        std::function<std::any()> getter;
        std::function<void(std::any)> setter;
    };

    class Component : public SpartanObject, public PooledObject<Component>
    {
    public:
        Component(Entity* entity);
        virtual ~Component() = default;

        // default returns a nil sol::reference, defined in Component.cpp where sol/sol.hpp is included
        virtual sol::reference AsLua(sol::state_view state);

        // called when the component gets added
        virtual void Initialize() {}

        virtual void PrepareGeometry() {}
        virtual bool PrepareWorld() { return true; }
        virtual bool StartsEarly() const { return false; }

        // called every time the simulation starts
        virtual void Start() {}

        // called every time the simulation stops
        virtual void Stop() {}

        // called when the component is removed
        virtual void Remove() {}

        // called every frame, before Tick, useful to reset states before the main update
        virtual void PreTick() {}

        // called every frame
        virtual void Tick() {}

        // Copy authored state; components with derived state can share their load/apply path.
        virtual void CopyFrom(const Component& source) { SetAttributes(source.GetAttributes()); }

        // called when the entity is being saved
        virtual void Save(pugi::xml_node& node) {}

        // called when the entity is being loaded
        virtual void Load(pugi::xml_node& node) {}

        template <typename T>
        static constexpr ComponentType TypeToEnum() { return ComponentType::Max; }

        static std::string TypeToString(ComponentType type)
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

        static ComponentType StringToType(const std::string& name)
        {
            #define X(type, str) if (name == #str) return ComponentType::type;
            SP_COMPONENT_LIST
            #undef X

            SP_LOG_ERROR("Unsupported component in this application: %s", name.c_str());
            return ComponentType::Max;
        }

        ComponentType GetType()          const { return m_type; }
        void SetType(ComponentType type)       { m_type = type; }

        const std::vector<Attribute>& GetAttributes() const { return m_attributes; }
        void SetAttributes(const std::vector<Attribute>& attributes);

        Entity* GetEntity() const { return m_entity_ptr; }

    protected:
        #define SP_REGISTER_ATTRIBUTE_GET_SET(getter, setter, type) RegisterAttribute(  \
        #getter, #type,                                                                 \
        [this]()                        { return getter(); },                           \
        [this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); }, true);     \

        #define SP_REGISTER_ATTRIBUTE_VALUE_SET(value, setter, type) RegisterAttribute( \
        #value, #type,                                                                  \
        [this]()                        { return value; },                              \
        [this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); }, true);     \

        #define SP_REGISTER_ATTRIBUTE_VALUE_VALUE(value, type) RegisterAttribute(       \
        #value, #type,                                                                  \
        [this]()                        { return value; },                              \
        [this](const std::any& valueIn) { value = std::any_cast<type>(valueIn); }, false);     \

        // registers an attribute
        void RegisterAttribute(const char* name, const char* type, std::function<std::any()>&& getter, std::function<void(std::any)>&& setter, bool writable = false)
        {
            Attribute attribute;
            attribute.writable = writable;
            attribute.name   = name;
            attribute.type   = type;
            attribute.getter = std::move(getter);
            attribute.setter = std::move(setter);
            m_attributes.emplace_back(attribute);
        }

        // the type of the component
        ComponentType m_type = ComponentType::Max;
        // the state of the component
        bool m_enabled       = false;
        // the owner of the component
        Entity* m_entity_ptr = nullptr;

    private:
        // the attributes of the component
        std::vector<Attribute> m_attributes;
    };

    // Component slots are known at compile time. Keeping the mapping here lets
    // GetComponent<T> compile to a fixed-offset pointer load in development builds.
    #define X(type, str) \
        class type; \
        template<> constexpr ComponentType Component::TypeToEnum<type>() { return ComponentType::type; }
    SP_COMPONENT_LIST
    #undef X
}
