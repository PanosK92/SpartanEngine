#/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===================
#include "ISubsystem.h"
#include "../Logging/Log.h"
#include "Spartan_Definitions.h"
//==============================

namespace Spartan
{
    class Engine;

    enum class TickType
    {
        Variable,
        Smoothed
    };

    struct _subystem
    {
        _subystem(const std::shared_ptr<ISubsystem>& subsystem, TickType tick_group)
        {
            ptr = subsystem;
            this->tick_group = tick_group;
        }

        std::shared_ptr<ISubsystem> ptr;
        TickType tick_group;
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

        template <class T>
        void AddSubsystem(TickType tick_group = TickType::Variable)
        {
            validate_subsystem_type<T>();

            m_subsystems.emplace_back(std::make_shared<T>(this), tick_group);
        }

        // Get a subsystem
        template <class T>
        T* GetSubsystem() const
        {
            validate_subsystem_type<T>();

            for (const auto& subsystem : m_subsystems)
            {
                if (subsystem.ptr)
                {
                    if (typeid(T) == typeid(*subsystem.ptr))
                        return static_cast<T*>(subsystem.ptr.get());
                }
            }

            return nullptr;
        }

        void OnInitialise()
        {
            std::vector<uint32_t> failed_indices;

            // Initialise subsystems
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_subsystems.size()); i++)
            {
                if (!m_subsystems[i].ptr->OnInitialise())
                {
                    failed_indices.emplace_back(i);
                    LOG_ERROR("Failed to initialize %s", typeid(*m_subsystems[i].ptr).name());
                }
            }

            // Removes that ones that failed
            for (const uint32_t failed_index : failed_indices)
            {
                m_subsystems.erase(m_subsystems.begin() + failed_index);
            }
        }

        void OnPreTick()
        {
            for (const _subystem& subsystem : m_subsystems)
            {
                subsystem.ptr->OnPreTick();
            }
        }

        void OnTick(TickType tick_group, float delta_time = 0.0f)
        {
            for (const _subystem& subsystem : m_subsystems)
            {
                if (subsystem.tick_group != tick_group)
                    continue;

                subsystem.ptr->OnTick(delta_time);
            }
        }

        void OnPostTick()
        {
            for (const _subystem& subsystem : m_subsystems)
            {
                subsystem.ptr->OnPostTick();
            }
        }

        void OnShutdown()
        {
            for (const _subystem& subsystem : m_subsystems)
            {
                subsystem.ptr->OnShutdown();
            }
        }

        Engine* m_engine = nullptr;

    private:
        std::vector<_subystem> m_subsystems;
    };
}
