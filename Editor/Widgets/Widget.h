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
        m_profiler  = m_context->GetSubsystem<Spartan::Profiler>();
        m_window    = nullptr;
    }
	virtual ~Widget() = default;

	bool Begin()
	{
        // Callback
        if (m_callback_on_start)
        {
            m_callback_on_start();
        }

		if (!m_is_window || !m_is_visible)
			return false;

        TIME_BLOCK_START_NAMED(m_profiler, m_title.c_str());

        // Callback
        if (m_callback_on_visible)
        {
            m_callback_on_visible();
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
		if (ImGui::Begin(m_title.c_str(), &m_is_visible, m_flags))
        {
            m_window    = ImGui::GetCurrentWindow();
            m_height    = ImGui::GetWindowHeight();
            m_begun     = true;
        }
        else if (m_window && m_window->Hidden)
        {
            // Enters here if the window is hidden as part of an unselected tab.
            // ImGui::Begin() makes the window and but returns false, then ImGui still expects ImGui::End() to be called.
            // So we make sure that when Widget::End() we call ImGui::End().
            // Note: ImGui's docking is in beta, so maybe it's at fault here ?
            m_begun = true;
        }

        // Begin callback
        if (m_begun && m_callback_on_begin)
        {
            m_callback_on_begin();
        }

        return m_begun;
	}

	virtual void Tick() = 0;

	bool End()
	{
        // End
        if (m_begun)
        {
		    ImGui::End();
        }

        // Pop style variables
        ImGui::PopStyleVar(m_var_pushes);
        m_var_pushes = 0;
		
        // End profiling
        TIME_BLOCK_END(m_profiler);

        // Reset state
        m_begun = false;

		return true;
	}

    template<typename T>
    void PushStyleVar(ImGuiStyleVar idx, T val) { ImGui::PushStyleVar(idx, val); m_var_pushes++; }

    // Properties
	bool IsWindow()                     const { return m_is_window; }
	bool& GetVisible()				          { return m_is_visible; }
	void SetVisible(bool is_visible)          { m_is_visible = is_visible; }
	float GetHeight()                   const { return m_height; }
	ImGuiWindow* GetWindow()            const { return m_window; }
	const auto& GetTitle()              const { return m_title; }

protected:
	bool m_is_visible	                        = true;
	bool m_is_window                            = true;	
	int m_flags	                                = ImGuiWindowFlags_NoCollapse;
	float m_height		                        = 0;
    float m_alpha                               = -1.0f;
    Spartan::Math::Vector2 m_position           = Spartan::Math::Vector2(-1.0f);
    Spartan::Math::Vector2 m_size               = Spartan::Math::Vector2(-1.0f);
    Spartan::Math::Vector2 m_size_max           = Spartan::Math::Vector2(FLT_MAX, FLT_MAX);
    Spartan::Math::Vector2 m_padding            = Spartan::Math::Vector2(-1.0f);
    std::function<void()> m_callback_on_start   = nullptr;
    std::function<void()> m_callback_on_visible = nullptr;
    std::function<void()> m_callback_on_begin   = nullptr;

	Spartan::Context* m_context     = nullptr;
    Spartan::Profiler* m_profiler   = nullptr;
	ImGuiWindow* m_window           = nullptr;
    std::string m_title;

private:
	bool m_begun            = false;
    uint8_t m_var_pushes    = 0;
};
