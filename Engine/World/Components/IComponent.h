/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================
#include <memory>
#include <string>
#include <any>
#include <vector>
#include <functional>
#include "../../Core/EngineDefs.h"
//================================

namespace Directus
{
	class Entity;
	class Transform;
	class Context;
	class FileStream;

	enum ComponentType : unsigned int
	{
		ComponentType_AudioListener,
		ComponentType_AudioSource,
		ComponentType_Camera,
		ComponentType_Collider,
		ComponentType_Constraint,
		ComponentType_Light,
		ComponentType_Renderable,
		ComponentType_RigidBody,
		ComponentType_Script,
		ComponentType_Skybox,
		ComponentType_Transform,
		ComponentType_Unknown
	};

	struct Attribute
	{
		std::function<std::any()> getter;
		std::function<void(std::any)> setter;
	};

	class ENGINE_CLASS IComponent
	{
	public:
		IComponent(Context* context, Entity* entity, Transform* transform);
		virtual ~IComponent() {}

		// Runs when the component gets added
		virtual void OnInitialize() {}

		// Runs every time the simulation starts
		virtual void OnStart() {}

		// Runs every time the simulation stops
		virtual void OnStop() {}

		// Runs when the component is removed
		virtual void OnRemove() {}

		// Runs every frame
		virtual void OnTick() {}

		// Runs when the entity is being saved
		virtual void Serialize(FileStream* stream) {}

		// Runs when the entity is being loaded
		virtual void Deserialize(FileStream* stream) {}

		//= PROPERTIES ==========================================================================
		Entity*						GetEntity_PtrRaw()		{ return m_entity; }	
		std::weak_ptr<Entity>		GetEntity_PtrWeak()		{ return GetEntity_PtrShared(); }
		std::shared_ptr<Entity>		GetEntity_PtrShared();

		Transform* GetTransform()			{ return m_transform; }
		Context* GetContext()				{ return m_context; }
		unsigned int GetID()				{ return m_ID; }
		void SetID(unsigned int id)			{ m_ID = id; }
		ComponentType GetType()				{ return m_type; }
		void SetType(ComponentType type)	{ m_type = type; }

		const std::string& GetentityName();

		template <typename T>
		static ComponentType Type_To_Enum();

		const auto& GetAttributes()	{ return m_attributes; }
		void SetAttributes(const std::vector<Attribute>& attributes)
		{ 
			for (unsigned int i = 0; i < (unsigned int)m_attributes.size(); i++)
			{
				m_attributes[i].setter(attributes[i].getter());
			}
		}
		//=======================================================================================

	protected:
		#define REGISTER_ATTRIBUTE_GET_SET(getter, setter, type) RegisterAttribute(		\
		[this]()						{ return getter(); },							\
		[this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); });		\

		#define REGISTER_ATTRIBUTE_VALUE_SET(value, setter, type) RegisterAttribute(	\
		[this]()						{ return value; },								\
		[this](const std::any& valueIn) { setter(std::any_cast<type>(valueIn)); });		\

		#define REGISTER_ATTRIBUTE_VALUE_VALUE(value, type) RegisterAttribute(			\
		[this]()						{ return value; },								\
		[this](const std::any& valueIn) { value = std::any_cast<type>(valueIn); });		\

		// Registers an attribute
		void RegisterAttribute(std::function<std::any()>&& getter, std::function<void(std::any)>&& setter)
		{ 
			Attribute attribute;
			attribute.getter = std::move(getter);
			attribute.setter = std::move(setter);
			m_attributes.emplace_back(attribute);
		}

		// The type of the component
		ComponentType m_type		= ComponentType_Unknown;
		// The id of the component
		unsigned int m_ID			= 0;
		// The state of the component
		bool m_enabled				= false;
		// The owner of the component
		Entity* m_entity				= nullptr;
		// The transform of the component (always exists)
		Transform* m_transform		= nullptr;
		// The context of the engine
		Context* m_context			= nullptr;

	private:
		// The attributes of the component
		std::vector<Attribute> m_attributes;
	};
}