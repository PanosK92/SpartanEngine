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

//= INCLUDES ====================
#include "Widget_Toolbar.h"
#include "Widget_Profiler.h"
#include "Widget_ResourceCache.h"
#include "Widget_ShaderEditor.h"
#include "Widget_RenderOptions.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
#include "Rendering/Renderer.h"
#include "../IconProvider.h"
#include "../ImGui_Extension.h"
//===============================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Toolbar::Widget_Toolbar(Context* context) : Widget(context)
{
	m_title = "Toolbar";
    
	m_flags =
        ImGuiWindowFlags_NoCollapse         |
		ImGuiWindowFlags_NoResize           |
		ImGuiWindowFlags_NoMove             |
		ImGuiWindowFlags_NoSavedSettings    |
		ImGuiWindowFlags_NoScrollbar        |
		ImGuiWindowFlags_NoTitleBar;

    m_begin_pre_callback = [this]()
    {
        auto& ctx = *ImGui::GetCurrentContext();
        ctx.NextWindowData.MenuBarOffsetMinVal = ImVec2( ctx.Style.DisplaySafeAreaPadding.x, Max(ctx.Style.DisplaySafeAreaPadding.y - ctx.Style.FramePadding.y, 0.0f));
        m_position  = Vector2(ctx.Viewports[0]->Pos.x, ctx.Viewports[0]->Pos.y + 25.0f);
        m_size      = Vector2(ctx.Viewports[0]->Size.x, ctx.NextWindowData.MenuBarOffsetMinVal.y + ctx.FontBaseSize + ctx.Style.FramePadding.y + 20.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Vector2(0, 5));
    };

    m_begin_post_callback = [this]()
    {
        ImGui::PopStyleVar();
    };

    m_widgets[Icon_Profiler]            = make_unique<Widget_Profiler>(context);
    m_widgets[Icon_ResourceCache]       = make_unique<Widget_ResourceCache>(context);
    m_widgets[Icon_Component_Script]    = make_unique<Widget_ShaderEditor>(context);
    m_widgets[Icon_Component_Options]   = make_unique<Widget_RenderOptions>(context);

	Engine::EngineMode_Disable(Engine_Game);
}

void Widget_Toolbar::Tick(float delta_time)
{
    auto show_button = [this](Icon_Type icon_type, function<bool()> get_visibility, function<void()> make_visible)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, get_visibility() ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGuiEx::ImageButton(icon_type, m_button_size))
        {
            make_visible();
        }
        ImGui::PopStyleColor();
    };

    // Play button	
    show_button(Icon_Button_Play, []() { return Engine::EngineMode_IsSet(Engine_Game); }, []() { Engine::EngineMode_Toggle(Engine_Game); });

    for (auto& widget_it : m_widgets)
    {
        Widget* widget          = widget_it.second.get();
        Icon_Type widget_icon   = widget_it.first;

        show_button(widget_icon, [this, &widget](){ return widget->GetVisible(); }, [this, &widget]() { widget->SetVisible(true); });

        if (widget->GetVisible())
        {
            widget->Begin();
            widget->Tick(delta_time);
            widget->End();
        }
    }
}
