#/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "ISystem.h"
#include "../Logging/Log.h"
#include "Definitions.h"
//=========================

namespace Spartan
{
    class Engine;

    enum class TickType
    {
        Variable,
        Smoothed
    };

    struct system_wrapper
    {
        system_wrapper(const std::shared_ptr<ISystem>& subsystem, TickType tick_group)
        {
            ptr = subsystem;
            this->tick_group = tick_group;
        }

        std::shared_ptr<ISystem> ptr;
        TickType tick_group;
    };

    // Contains all subsystems
    class SP_CLASS Context
    {
    public:
        Context() = default;

        ~Context()
        {
            // Loop in reverse registration order to avoid dependency conflicts
            for (size_t i = m_systems.size() - 1; i > 0; i--)
            {
                m_systems[i].ptr.reset();
            }

            m_systems.clear();
        }

        template <class T>
        void AddSystem(TickType tick_group = TickType::Variable)
        {
            validate_subsystem_type<T>();

            m_systems.emplace_back(std::make_shared<T>(this), tick_group);
        }

        // Get a subsystem
        template <class T>
        T* GetSystem() const
        {
            validate_subsystem_type<T>();

            for (const auto& subsystem : m_systems)
            {
                if (subsystem.ptr)
                {
                    if (typeid(T) == typeid(*subsystem.ptr))
                        return static_cast<T*>(subsystem.ptr.get());
                }
            }

            return nullptr;
        }

        void OnInitialize()
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                subsystem.ptr->OnInitialise();
            }
        }

        void OnPostInitialize()
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                subsystem.ptr->OnPostInitialise();
            }
        }

        void OnPreTick()
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                subsystem.ptr->OnPreTick();
            }
        }

        void OnTick(TickType tick_group, double delta_time)
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                if (subsystem.tick_group != tick_group)
                    continue;

                subsystem.ptr->OnTick(delta_time);
            }
        }

        void OnPostTick()
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                subsystem.ptr->OnPostTick();
            }
        }

        void OnShutdown()
        {
            for (const system_wrapper& subsystem : m_systems)
            {
                subsystem.ptr->OnShutdown();
            }
        }

        Engine* m_engine = nullptr;

    private:
        std::vector<system_wrapper> m_systems;
    };
}
