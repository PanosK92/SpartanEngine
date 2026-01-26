/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ========================
#include <any>
#include <vector>
#include <functional>
#include "../../Core/SpartanObject.h"
//===================================

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    class Entity;
    class FileStream;

    // X-Macro: single source of truth for all components
    // Format: X(ClassName, string_name)
    // To add a new component, just add a line here
    #define SP_COMPONENT_LIST       \
        X(AudioSource, audio_source) \
        X(Camera,      camera)       \
        X(Light,       light)        \
        X(Physics,     physics)      \
        X(Renderable,  renderable)   \
        X(Terrain,     terrain)      \
        X(Volume,      volume)

    enum class ComponentType : uint32_t
    {
        #define X(type, str) type,
        SP_COMPONENT_LIST
        #undef X
        Max
    };

    struct Attribute
    {
        std::function<std::any()> getter;
        std::function<void(std::any)> setter;
    };

    class Component : public SpartanObject
    {
    public:
        Component(Entity* entity);
        virtual ~Component() = default;

        // called when the component gets added
        virtual void Initialize() {}

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

        // called when the entity is being saved
        virtual void Save(pugi::xml_node& node) {}

        // called when the entity is being loaded
        virtual void Load(pugi::xml_node& node) {}

        template <typename T>
        static ComponentType TypeToEnum();

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
        
            assert(false && "StringToType: Unknown component name");
            return ComponentType::Max;
        }

        ComponentType GetType()          const { return m_type; }
        void SetType(ComponentType type)       { m_type = type; }

        const auto& GetAttributes() const { return m_attributes; }
        void SetAttributes(const std::vector<Attribute>& attributes)
        { 
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_attributes.size()); i++)
            {
                m_attributes[i].setter(attributes[i].getter());
            }
        }

        Entity* GetEntity() const { return m_entity_ptr; }
 
    protected:
        #define SP_REGISTER_ATTRIBUTE_GET_SET(getter, setter, type) RegisterAttribute(  \
        [this]()                        { return getter(); },                           \
        [this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); });     \

        #define SP_REGISTER_ATTRIBUTE_VALUE_SET(value, setter, type) RegisterAttribute( \
        [this]()                        { return value; },                              \
        [this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); });     \

        #define SP_REGISTER_ATTRIBUTE_VALUE_VALUE(value, type) RegisterAttribute(       \
        [this]()                        { return value; },                              \
        [this](const std::any& valueIn) { value = std::any_cast<type>(valueIn); });     \

        // registers an attribute
        void RegisterAttribute(std::function<std::any()>&& getter, std::function<void(std::any)>&& setter)
        { 
            Attribute attribute;
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
}
