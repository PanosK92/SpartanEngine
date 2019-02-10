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

//= INCLUDES =================================
#include <string>
#include "../../ImGui/Source/imgui.h"
#include "../../ImGui/Source/imgui_internal.h"
//============================================

namespace Directus	{ class Context; }

class Widget
{
public:
	Widget(Directus::Context* context) { m_context	= context; }
	virtual ~Widget() {}

	virtual bool Begin()
	{
		if (!m_isWindow || !m_isVisible)
			return false;

		ImGui::SetNextWindowSize(ImVec2(m_xMin, m_yMin), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(m_xMin, m_yMin), ImVec2(m_xMax, m_yMax));
		ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);
		m_windowBegun = true;

		return true;
	}

	virtual void Tick(float deltaTime = 0.0f) = 0;

	virtual bool End()
	{
		// Sometimes a window can become invisible during it's lifetime (e.g. clicking the x button).
		// In these cases, m_windowBegun will be true and we have to call ImGui::End() anyway.
		if ((!m_isWindow || !m_isVisible) && !m_windowBegun)
			return false;

		m_window = ImGui::GetCurrentWindow();
		m_height = ImGui::GetWindowHeight();
		ImGui::End();
		m_windowBegun = false;

		return true;
	}

	bool IsWindow()					{ return m_isWindow; }
	bool& GetVisible()				{ return m_isVisible; }
	void SetVisible(bool isVisible) { m_isVisible = isVisible; }
	float GetHeight()				{ return m_height; }
	ImGuiWindow* GetWindow()		{ return m_window; }
	const std::string& GetTitle()	{ return m_title; }

protected:
	bool m_isVisible	= true;
	bool m_isWindow		= true;	
	int m_windowFlags	= ImGuiWindowFlags_NoCollapse;
	float m_xMin		= 0;
	float m_xMax		= FLT_MAX;
	float m_yMin		= 0;
	float m_yMax		= FLT_MAX;
	float m_height		= 0;

	Directus::Context* m_context = nullptr;
	std::string m_title;
	ImGuiWindow* m_window;

private:
	bool m_windowBegun = false;
};