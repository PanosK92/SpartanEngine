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
#include <vector>
#include "EngineDefs.h"
#include "ISubsystem.h"
#include "../Logging/Log.h"
//=========================

namespace Directus
{
	#define ValidateSubsystemType(T) static_assert(std::is_base_of<ISubsystem, T>::value, "Provided type does not implement ISubystem")

	class ENGINE_CLASS Context
	{
	public:
		Context() {}
		~Context() { m_subsystems.clear(); }

		// Register a subsystem
		template <class T>
		void RegisterSubsystem()
		{
			ValidateSubsystemType(T);
			m_subsystems.emplace_back(std::make_shared<T>(this));
		}

		// Initialize subsystems
		bool Initialize()
		{
			bool result = true;
			for (const auto& subsystem : m_subsystems)
			{
				if (!subsystem->Initialize())
				{
					LOGF_ERROR("Failed to initialize %s", typeid(*subsystem).name());
					result = false;
				}
			}

			return result;
		}

		// Tick subsystems
		void Tick()
		{
			for (const auto& subsystem : m_subsystems)
			{
				subsystem->Tick();
			}
		}

		// Get a subsystem
		template <class T> 
		std::shared_ptr<T> GetSubsystem()
		{
			ValidateSubsystemType(T);
			for (const auto& subsystem : m_subsystems)
			{
				if (typeid(T) == typeid(*subsystem))
					return std::static_pointer_cast<T>(subsystem);
			}

			return nullptr;
		}

	private:
		std::vector<std::shared_ptr<ISubsystem>> m_subsystems;
	};
}