/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==============================
#include <string>
#include <functional>
#include "../ImGui/Source/imgui.h"
#include "../ImGui/Source/imgui_internal.h"
#include "Profiling/Profiler.h"
#include "Core/Context.h"
//=========================================

namespace Spartan { class Context; }

class Widget
{
public:
	Widget(Spartan::Context* context)
    {
        m_context   = context;
        m_profiler  = m_context->GetSubsystem<Spartan::Profiler>().get();
        m_window    = nullptr;
    }
	virtual ~Widget() = default;

	bool Begin()
	{
        // Callback
        if (m_callback_begin_visibility)
        {
            m_callback_begin_visibility();
        }

		if (!m_is_window || !m_is_visible)
			return false;

        TIME_BLOCK_START_CPU_NAMED(m_profiler, m_title.c_str());

        // Reset
        m_var_pushes = 0;

        // Callback
        if (m_callback_begin_pre)
        {
            m_callback_begin_pre();
        }

        // Position
        if (m_position.x != -1.0f && m_position.y != -1.0f)
        {
            ImGui::SetNextWindowPos(m_position);
        }

        // Padding
        if (m_padding.x != -1.0f && m_padding.y != -1.0f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_padding);
            m_var_pushes++;
        }

        // Alpha
        if (m_alpha != -1.0f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_alpha);
            m_var_pushes++;
        }

        // Size
        if (m_size.x != -1.0f && m_size.y != -1.0f)
        {
            ImGui::SetNextWindowSize(m_size, ImGuiCond_FirstUseEver);
        }

        // Max size
        if ((m_size.x != -1.0f && m_size.y != -1.0f) || (m_size_max.x != FLT_MAX && m_size_max.y != FLT_MAX))
        {
            ImGui::SetNextWindowSizeConstraints(m_size, m_size_max);
        }

        // Begin
		ImGui::Begin(m_title.c_str(), &m_is_visible, m_flags);
		m_window_begun = true;

        if (m_callback_begin_post)
        {
            m_callback_begin_post();
        }

		return true;
	}

	virtual void Tick() = 0;

	bool End()
	{
        if (!m_window_begun)
            return false;

		m_window = ImGui::GetCurrentWindow();
		m_height = ImGui::GetWindowHeight();

        // End
		ImGui::End();
        ImGui::PopStyleVar(m_var_pushes);
		m_window_begun = false;

        TIME_BLOCK_END(m_profiler);

		return true;
	}

    template<typename T>
    void PushStyleVar(ImGuiStyleVar idx, T val) { ImGui::PushStyleVar(idx, val); m_var_pushes++; }

    // Properties
	auto IsWindow()					   { return m_is_window; }
	auto& GetVisible()				   { return m_is_visible; }
	void SetVisible(bool is_visible)   { m_is_visible = is_visible; }
	auto GetHeight()				   { return m_height; }
	auto GetWindow()		           { return m_window; }
	const auto& GetTitle()	           { return m_title; }

protected:
	bool m_is_visible	                                = true;
	bool m_is_window                                    = true;	
	int m_flags	                                        = ImGuiWindowFlags_NoCollapse;
	float m_height		                                = 0;
    float m_alpha                                       = -1.0f;
    Spartan::Math::Vector2 m_position                   = Spartan::Math::Vector2(-1.0f);
    Spartan::Math::Vector2 m_size                       = Spartan::Math::Vector2(-1.0f);
    Spartan::Math::Vector2 m_size_max                   = Spartan::Math::Vector2(FLT_MAX, FLT_MAX);
    Spartan::Math::Vector2 m_padding                    = Spartan::Math::Vector2(-1.0f);
    std::function<void()> m_callback_begin_visibility   = nullptr;
    std::function<void()> m_callback_begin_pre          = nullptr;
    std::function<void()> m_callback_begin_post         = nullptr;

	Spartan::Context* m_context     = nullptr;
    Spartan::Profiler* m_profiler   = nullptr;
	ImGuiWindow* m_window           = nullptr;
    std::string m_title;

private:
	bool m_window_begun     = false;
    uint8_t m_var_pushes    = 0;
};
