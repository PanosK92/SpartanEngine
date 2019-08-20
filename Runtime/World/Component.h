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
#include "../Core/EngineDefs.h"
#include "Components/Transform.h"
#include "Components/IComponent.h"
#include "../IO/FileStream.h"
#include <memory>
#include <array>
#include <vector>
#include <iostream>
#include <functional>
#include "../Logging/Log.h"
//================================

namespace Spartan
{
    template <typename T>
	struct SPARTAN_CLASS ComponentData
	{
		unsigned int mSize = 1;
		std::array<std::shared_ptr<T>, 1024> mData;
    };

    enum class DuplicateMode : char
    {
        None,
        Override
    };

	// Simple unsigned int (syntactical sugar) that refers to the index of a component in the componentData array
	typedef uint32_t ComponentIndex;

	class BaseComponentManager 
	{
    public:
        ComponentType m_type;

    public:
        BaseComponentManager(ComponentType type) : m_type(type) {}
        BaseComponentManager() = default;
		virtual ~BaseComponentManager() = default;

        virtual void AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component, DuplicateMode mode = DuplicateMode::None) = 0;

        virtual std::shared_ptr<IComponent> GetComponent(uint32_t entityID) = 0;
        virtual std::shared_ptr<IComponent> GetComponentByID(uint32_t entityID, uint32_t componentID) = 0;
        virtual std::vector<std::shared_ptr<IComponent>> GetComponents(uint32_t entityID) = 0;

        virtual void RemoveComponent(uint32_t entityID) = 0;
        virtual void RemoveComponentByID(uint32_t entityID, uint32_t componentID) = 0;

        virtual void Iterate(std::function<void(std::shared_ptr<IComponent>)> func) = 0;
        virtual void Clear() = 0;

        BaseComponentManager(const BaseComponentManager&) = default;
        BaseComponentManager& operator=(const BaseComponentManager&) = default;

        BaseComponentManager(BaseComponentManager&&) = default;
        BaseComponentManager& operator=(BaseComponentManager&&) = default;

    protected:
        ComponentData<IComponent> mComponentData;

        std::unordered_map<uint32_t, std::unordered_map<uint32_t, ComponentIndex>> mEntityMap;
        std::unordered_map<ComponentIndex, uint32_t> mInstanceMap;
	};

	template<typename T>
	class SPARTAN_CLASS ComponentManager : public BaseComponentManager
	{
	public:
        ComponentManager() : BaseComponentManager(IComponent::TypeToEnum<T>()) {}

		void AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component, DuplicateMode mode = DuplicateMode::None) override;

        std::shared_ptr<IComponent> GetComponent(uint32_t entityID);
        std::shared_ptr<IComponent> GetComponentByID(uint32_t entityID, uint32_t componentID);
        std::vector<std::shared_ptr<IComponent>> GetComponents(uint32_t entityID);

	    void RemoveComponent(uint32_t entityID) override;
        void RemoveComponentByID(uint32_t entityID, uint32_t componentID) override;

		void Iterate(std::function<void(std::shared_ptr<IComponent>)> func) override;
        void Clear() override { mEntityMap.clear(); mInstanceMap.clear(); mComponentData.mSize = 1; }
	};
		
	template<typename T>
	inline void ComponentManager<T>::AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component, DuplicateMode mode)
	{
        switch (mode)
        {
        case DuplicateMode::None:
        {
            ComponentIndex instance = mComponentData.mSize;

            if (mEntityMap[entityID].empty())
            {
                mComponentData.mData[instance] = component;
                mEntityMap[entityID][component->GetId()] = instance;
                mInstanceMap[instance] = entityID;

                mComponentData.mSize++;
            }
        }
        case DuplicateMode::Override:
        {
            if (!mEntityMap[entityID].empty())
            {
                ComponentIndex index = mEntityMap[entityID].begin()->second;
                mEntityMap[entityID].erase(mEntityMap[entityID].begin()->first);
                mEntityMap[entityID][component->GetId()] = index;
                mComponentData.mData[index] = component;
            }
            else
            {
                ComponentIndex instance = mComponentData.mSize;

                mComponentData.mData[instance] = component;
                mEntityMap[entityID][component->GetId()] = instance;
                mInstanceMap[instance] = entityID;

                mComponentData.mSize++;
            }
        }
        }
	}
		
	template<typename T>
	inline std::shared_ptr<IComponent> ComponentManager<T>::GetComponent(uint32_t entity_id)
	{
        std::unordered_map<uint32_t, ComponentIndex> _map = mEntityMap[entity_id];		
        if (!_map.empty())
		{
			return mComponentData.mData[_map.begin()->second];
		}

        static std::shared_ptr<T> empty;
        return std::dynamic_pointer_cast<IComponent>(empty);
	}

    template<typename T>
    inline std::shared_ptr<IComponent> ComponentManager<T>::GetComponentByID(uint32_t entity_id, uint32_t componentID)
    {
        auto _map = mEntityMap[entity_id];
        if (!(_map.count(componentID) > 0))
        {
            ComponentIndex i = _map[componentID];
            return mComponentData.mData[i];
        }

        return nullptr;
    }

    template<typename T>
    inline std::vector<std::shared_ptr<IComponent>> ComponentManager<T>::GetComponents(uint32_t entity_id)
    {
        auto _map = mEntityMap[entity_id];
        std::vector<std::shared_ptr<IComponent>> components;
        if (!_map.empty())
        {
            for (auto& c : _map)
            {
                components.push_back(mComponentData.mData[c.second]);
            }
        }

        return components;
    }
		
	template<typename T>
	inline void ComponentManager<T>::RemoveComponent(uint32_t entity_id)
	{
        std::unordered_map<uint32_t, ComponentIndex> _map = mEntityMap[entity_id];
        if (!_map.empty())
        {
            mComponentData.mData[mEntityMap[entity_id].begin()->second] = nullptr;

            if ((mEntityMap[entity_id].begin()->second) == (mComponentData.mSize - 1))
                mComponentData.mSize--;

            mEntityMap[entity_id].erase(mEntityMap[entity_id].begin());
        }
        else
        {
            __debugbreak();
        }
    }

    template<typename T>
    inline void ComponentManager<T>::RemoveComponentByID(uint32_t entity_id, uint32_t componentID)
    {
        std::unordered_map<uint32_t, ComponentIndex> _map = mEntityMap[entity_id];
        if (_map.count(componentID) > 0)
        {
            ComponentIndex lastComponent = mComponentData.mSize - 1;
            mComponentData.mData[_map[componentID]] = nullptr;      

            if ((mEntityMap[entity_id][componentID]) == (mComponentData.mSize - 1))
                mComponentData.mSize--;

            mEntityMap[entity_id].erase(componentID);
        }
    }
		
	template<typename T>
    inline void ComponentManager<T>::Iterate(std::function<void(std::shared_ptr<IComponent>)> func)
	{
		for (uint32_t i = 1; i < mComponentData.mSize; i++)
		{
            std::shared_ptr<IComponent> component = mComponentData.mData[i];
            if (component != nullptr)
            {
                func(component);
            }
		}
	}
}
