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
#include "EngineDefs.h"
#include "ISubsystem.h"
#include "../Logging/Log.h"
//=========================

namespace Spartan
{
    class Engine;

    enum Tick_Group
    {
        Tick_Variable,
        Tick_Smoothed
    };

    struct _subystem
    {
        _subystem(const std::shared_ptr<ISubsystem>& subsystem, Tick_Group tick_group)
        {
            ptr = subsystem;
            this->tick_group = tick_group;
        }

        std::shared_ptr<ISubsystem> ptr;
        Tick_Group tick_group;
    };

	class SPARTAN_CLASS Context
	{
	public:
		Context() = default;

		~Context()
        {
            // Loop in reverse registration order to avoid dependency conflicts
            for (size_t i = m_subsystems.size() - 1; i > 0; i--)
            {
                m_subsystems[i].ptr.reset();
            }

            m_subsystems.clear();
        }

		// Register a subsystem
		template <class T>
		void RegisterSubsystem(Tick_Group tick_group = Tick_Variable)
		{
            validate_subsystem_type<T>();

            m_subsystems.emplace_back(std::make_shared<T>(this), tick_group);
		}

		// Initialize subsystems
		bool Initialize()
		{
			auto result = true;
            for (const auto& subsystem : m_subsystems)
            {
                if (!subsystem.ptr->Initialize())
                {
                	LOG_ERROR("Failed to initialize %s", typeid(*subsystem.ptr).name());
                	result = false;
                }
            }

			return result;
		}

        // Tick
		void Tick(Tick_Group tick_group, float delta_time = 0.0f)
		{
            for (const auto& subsystem : m_subsystems)
            {
                if (subsystem.tick_group != tick_group)
                    continue;

                subsystem.ptr->Tick(delta_time);
            }
		}

		// Get a subsystem
		template <class T> 
		std::shared_ptr<T> GetSubsystem()
		{
            validate_subsystem_type<T>();

			for (const auto& subsystem : m_subsystems)
			{
                if (typeid(T) == typeid(*subsystem.ptr))
                    return std::static_pointer_cast<T>(subsystem.ptr);
			}

			return nullptr;
		}

        Engine* m_engine = nullptr;

	private:
		std::vector<_subystem> m_subsystems;
	};
}
