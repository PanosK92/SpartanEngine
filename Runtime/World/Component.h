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
#include "Components/IComponent.h"
#include <memory>
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

        virtual void AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component) = 0;

        virtual std::shared_ptr<IComponent> GetComponent(uint32_t entityID) = 0;
        virtual std::shared_ptr<IComponent> GetComponentByID(uint32_t entityID, uint32_t componentID) = 0;
        virtual std::vector<std::shared_ptr<IComponent>> GetComponents(uint32_t entityID) = 0;

        virtual void RemoveComponent(uint32_t entityID) = 0;
        virtual void RemoveComponentByID(uint32_t entityID, uint32_t componentID) = 0;

        virtual void Iterate(std::function<void(std::shared_ptr<IComponent>)> func) = 0;

		BaseComponentManager(const BaseComponentManager&) = default;
		BaseComponentManager& operator=(const BaseComponentManager&) = default;
			
		BaseComponentManager(BaseComponentManager&&) = default;
		BaseComponentManager& operator=(BaseComponentManager&&) = default;    
	};

	template<typename T>
	class SPARTAN_CLASS ComponentManager : public BaseComponentManager
	{
	public:
        ComponentManager() : BaseComponentManager(IComponent::TypeToEnum<T>()) {}

		void AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component) override;

        std::shared_ptr<T>& GetComponent(uint32_t entityID);
        std::shared_ptr<T>& GetComponentByID(uint32_t entityID, uint32_t componentID);
        std::vector<std::shared_ptr<T>> GetComponents(uint32_t entityID);

	    void RemoveComponent(uint32_t entityID) override;
        void RemoveComponentByID(uint32_t entityID, uint32_t componentID) override;

		void Iterate(std::function<void(std::shared_ptr<IComponent>)> func) override;

        //TODO: void Serialize(FileStream* stream); 
        //TODO: void Deserialize(FileStream* stream, Transform* parent);

	private:
		ComponentData<T> mComponentData;
			
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, ComponentIndex>> mEntityMap;
		std::unordered_map<ComponentIndex, uint32_t> mInstanceMap;
	};
		
	template<typename T>
	inline void ComponentManager<T>::AddComponent(uint32_t entityID, std::shared_ptr<IComponent>& component)
	{
		ComponentIndex instance = mComponentData.mSize;
		mComponentData.mData[instance] = component;
			
		mEntityMap[entityID][component->GetId()] = instance;
		mInstanceMap[instance] = entityID;
			
        mComponentData.mSize++;
	}
		
	template<typename T>
	inline std::shared_ptr<T>& ComponentManager<T>::GetComponent(uint32_t entity_id)
	{
        std::unordered_map<uint32_t, ComponentIndex> _map = mEntityMap[entity_id];		
		if (!_map.empty())
		{
			return mComponentData.mData[_map.begin()->second];
		}

        static std::shared_ptr<T> empty;
        return empty;
	}

    template<typename T>
    inline std::shared_ptr<T>& ComponentManager<T>::GetComponentByID(uint32_t entity_id, uint32_t componentID)
    {
        auto _map = mEntityMap[entity_id];
        if (!_map.count(componentID) > 0)
        {
            ComponentIndex i = _map[componentID];
            return mComponentData.mData[i];
        }

        return nullptr;
    }

    template<typename T>
    inline std::vector<std::shared_ptr<T>> ComponentManager<T>::GetComponents(uint32_t entity_id)
    {
        auto _map = mEntityMap[entity_id];
        std::vector<std::shared_ptr<T>> components;
        if (!_map.empty())
        {
            for (auto& c : _map)
            {
                components.push_back(std::dynamic_pointer_cast<IComponent>(mComponentData.mData[c.second]));
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
            ComponentIndex lastComponent = mComponentData.mSize - 1;
            mComponentData.mData[mEntityMap[entity_id].begin()->second] = mComponentData.mData[lastComponent];
            //mEntityMap[mInstanceMap[lastComponent]][mComponentData.mData[lastComponent]->GetId()] = componentID; // componentID not defined ?
            mComponentData.mData[lastComponent] = nullptr;
            mEntityMap[entity_id].erase(mEntityMap[entity_id].begin());

            mComponentData.mSize--;
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
            mComponentData.mData[_map[componentID]] = mComponentData.mData[lastComponent];
            mEntityMap[mInstanceMap[lastComponent]][mComponentData.mData[lastComponent]->GetId()] = componentID;
            mComponentData.mData[lastComponent] = nullptr;
            mEntityMap[entity_id].erase(componentID);

            mComponentData.mSize--;
        }
    }
		
	template<typename T>
    inline void ComponentManager<T>::Iterate(std::function<void(std::shared_ptr<IComponent>)> func)
	{
		for (uint32_t i = 1; i < mComponentData.mSize; i++)
		{
            std::shared_ptr<IComponent> component = std::dynamic_pointer_cast<IComponent>(mComponentData.mData[i]);
            func(component);
		}
	}
}
