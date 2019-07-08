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

//= INCLUDES ==============
#include <map>
#include "EngineDefs.h"
#include "ISubsystem.h"
#include "../Logging/Log.h"
//=========================

namespace Spartan
{
    class Engine;
	#define VALIDATE_SUBSYSTEM_TYPE(T) static_assert(std::is_base_of<ISubsystem, T>::value, "Provided type does not implement ISubystem")

    enum Tick_Group
    {
        Tick_Variable,
        Tick_Smoothed
    };

	class SPARTAN_CLASS Context
	{
	public:
		Context() = default;
		~Context() { m_subsystem_groups.clear(); }

		// Register a subsystem
		template <class T>
		void RegisterSubsystem(Tick_Group tick_group = Tick_Variable)
		{
			VALIDATE_SUBSYSTEM_TYPE(T);
			m_subsystem_groups[tick_group].emplace_back(std::make_shared<T>(this));
		}

		// Initialize subsystems
		bool Initialize()
		{
			auto result = true;
			for (const auto& group : m_subsystem_groups)
			{
                for (const auto& subsystem : group.second)
                {
				    if (!subsystem->Initialize())
				    {
				    	LOGF_ERROR("Failed to initialize %s", typeid(*subsystem).name());
				    	result = false;
				    }
                }
			}

			return result;
		}

        // Tick
		void Tick(Tick_Group tick_group, float delta_time = 0.0f)
		{
            for (const auto& subsystem : m_subsystem_groups[tick_group])
            {
                subsystem->Tick(delta_time);
            }
		}

		// Get a subsystem
		template <class T> 
		std::shared_ptr<T> GetSubsystem()
		{
			VALIDATE_SUBSYSTEM_TYPE(T);
			for (const auto& group : m_subsystem_groups)
			{
                for (const auto& subsystem : group.second)
                {
				    if (typeid(T) == typeid(*subsystem))
					    return std::static_pointer_cast<T>(subsystem);
                }
			}

			return nullptr;
		}

        Engine* m_engine = nullptr;

	private:
		std::map<Tick_Group, std::vector<std::shared_ptr<ISubsystem>>> m_subsystem_groups;
	};
}
