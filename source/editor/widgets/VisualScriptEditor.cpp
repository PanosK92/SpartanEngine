/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#include "VisualScriptEditor.h"
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include "../Editor.h"
#include "../Selection.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Properties.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/source/imgui_stdlib.h"
#include "file_system/FileSystem.h"
#include "scripting/visual/VisualEdit.h"
#include "scripting/visual/VisualScript.h"
#include "world/Entity.h"
#include "world/World.h"
#include "world/components/Script.h"
//==============================================

//= NAMESPACES ===========================
using namespace std;
using namespace spartan;
using namespace spartan::scripting::visual;
//========================================

namespace
{
    // node metrics in canvas units, the canvas scales them by the zoom
    constexpr float k_header       = 26.0f;
    constexpr float k_row          = 24.0f;
    constexpr float k_pad          = 10.0f;
    constexpr float k_pin_radius   = 5.0f;
    constexpr float k_min_width    = 120.0f;
    constexpr float k_grid         = 32.0f;
    constexpr float k_zoom_min     = 0.25f;
    constexpr float k_zoom_max     = 2.0f;
    constexpr float k_side_width   = 280.0f;
    constexpr float k_comment_w    = 320.0f;
    constexpr float k_comment_h    = 180.0f;
    constexpr size_t k_undo_depth  = 128;
    constexpr size_t k_max_results = 300;

    ImVec2 add(const ImVec2& a, const ImVec2& b)  { return ImVec2(a.x + b.x, a.y + b.y); }
    ImVec2 sub(const ImVec2& a, const ImVec2& b)  { return ImVec2(a.x - b.x, a.y - b.y); }
    ImVec2 mul(const ImVec2& a, float s)          { return ImVec2(a.x * s, a.y * s); }

    string lower(string text)
    {
        transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return text;
    }

    string number_text(double value)
    {
        char text[64];
        snprintf(text, sizeof(text), "%.6g", value);
        return text;
    }

    vector<float> parse_floats(const string& text, size_t count)
    {
        vector<float> values;
        const char* cursor = text.c_str();
        while (*cursor && values.size() < count)
        {
            char* end   = nullptr;
            float value = strtof(cursor, &end);
            if (end == cursor)
            {
                cursor++;
                continue;
            }
            values.push_back(value);
            cursor = end;
        }
        values.resize(count, 0.0f);
        return values;
    }

    size_t vector_size(const string& type)
    {
        if (type == "Vector2")
        {
            return 2;
        }
        if (type == "Vector3" || type == "Quaternion")
        {
            return 3;
        }
        if (type == "Vector4")
        {
            return 4;
        }
        return 0;
    }

    float inline_width(const string& type)
    {
        if (type == "boolean")
        {
            return 22.0f;
        }
        if (type == "number" || type == "integer")
        {
            return 64.0f;
        }
        if (const size_t count = vector_size(type))
        {
            return 52.0f * static_cast<float>(count);
        }
        if (scripting::visual::is_enum(type))
        {
            return 120.0f;
        }
        return 110.0f;
    }

    // exec pins named exec and then read fine without a label, like every other node editor
    bool pin_label_visible(const PinDefinition& pin, bool output)
    {
        if (pin.type == type_exec)
        {
            return output ? pin.name != "then" : pin.name != "exec";
        }
        return true;
    }

    ImU32 color(int r, int g, int b, int a = 255)
    {
        return IM_COL32(r, g, b, a);
    }

    ImU32 pin_color(const string& type)
    {
        if (type == type_exec)            return color(235, 235, 235);
        if (type == "boolean")            return color(205, 50, 50);
        if (type == "integer")            return color(40, 215, 165);
        if (type == "number")             return color(150, 225, 60);
        if (type == "string")             return color(235, 70, 200);
        if (vector_size(type) && type != "Quaternion") return color(250, 200, 40);
        if (type == "Quaternion")         return color(150, 160, 255);
        if (type == "Entity")             return color(50, 150, 255);
        if (type == "table")              return color(185, 140, 100);
        if (type == "any" || type.empty()) return color(170, 170, 170);
        if (scripting::visual::is_enum(type)) return color(30, 140, 120);

        // usertypes get a stable color of their own
        uint32_t hash = 2166136261u;
        for (char c : type)
        {
            hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        float r = 0.0f, g = 0.0f, b = 0.0f;
        ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash % 360) / 360.0f, 0.55f, 0.95f, r, g, b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
    }

    ImU32 with_alpha(ImU32 value, float alpha)
    {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(value);
        c.w     *= alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    void draw_pin_shape(ImDrawList* draw, const ImVec2& center, float radius, const string& type, bool connected, float alpha, bool hovered = false)
    {
        const ImU32 col = with_alpha(pin_color(type), alpha);
        if (hovered)
        {
            draw->AddCircleFilled(center, radius * 2.4f, with_alpha(pin_color(type), 0.18f * alpha));
            draw->AddCircleFilled(center, radius * 1.6f, with_alpha(pin_color(type), 0.22f * alpha));
        }
        if (type == type_exec)
        {
            const ImVec2 a(center.x - radius, center.y - radius * 1.1f);
            const ImVec2 b(center.x + radius * 1.1f, center.y);
            const ImVec2 c(center.x - radius, center.y + radius * 1.1f);
            if (connected)
            {
                draw->AddTriangleFilled(a, b, c, col);
            }
            else
            {
                draw->AddTriangle(a, b, c, col, 1.5f);
            }
            return;
        }

        if (connected)
        {
            draw->AddCircleFilled(center, radius, col);
        }
        else
        {
            draw->AddCircle(center, radius, col, 0, 1.5f);
        }
    }

    float comment_size(const Node& node, const char* key, float fallback)
    {
        auto it = node.data.find(key);
        return it != node.data.end() ? max(80.0f, strtof(it->second.c_str(), nullptr)) : fallback;
    }

    bool is_lifecycle_event(const NodeDefinition& definition)
    {
        return definition.kind == NodeKind::Event;
    }

    ImVec4 to_vec4(ImU32 value)
    {
        return ImGui::ColorConvertU32ToFloat4(value);
    }

    // a soft penumbra dropped a little below a pane, the light comes from above like everywhere else in the editor
    void draw_drop_shadow(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float rounding, float zoom)
    {
        const float drop = 4.0f * zoom;
        ImGui::EditorUi::draw_glow(draw, ImVec2(min.x, min.y + drop), ImVec2(max.x, max.y + drop), ImVec4(0.0f, 0.0f, 0.0f, 1.0f), rounding, 14.0f * zoom, 1.4f);
    }

    // a node is a pane of smoked glass: a header tinted with its kind, a body that darkens toward the bottom and an edge that catches the light
    void draw_glass_node(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float header, float rounding, float zoom, const ImVec4& hue, bool selected, bool hovered, const ImVec4* alert)
    {
        draw_drop_shadow(draw, min, max, rounding, zoom);
        if (selected)
        {
            ImGui::EditorUi::draw_glow(draw, min, max, ImGui::Style::color_accent_1, rounding, 12.0f * zoom, 1.2f);
        }
        else if (alert)
        {
            ImGui::EditorUi::draw_glow(draw, min, max, *alert, rounding, 10.0f * zoom, 1.0f);
        }

        const ImVec4 base   = ImGui::Style::with_alpha(ImGui::Style::color_canvas_deep, 1.0f);
        const float lift    = hovered || selected ? 0.05f : 0.0f;
        const int body_vtx  = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding);
        ImGui::EditorUi::shade_vertical(draw, body_vtx, min.y, max.y, ImGui::Style::with_alpha(ImGui::Style::lerp(base, hue, 0.10f + lift), 0.90f), ImGui::Style::with_alpha(base, 0.94f));

        const ImVec2 header_max = ImVec2(max.x, min.y + header);
        const int header_vtx    = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, header_max, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersTop);
        ImGui::EditorUi::shade_vertical(draw, header_vtx, min.y, header_max.y, ImGui::Style::with_alpha(ImGui::Style::lerp(base, hue, 0.70f + lift), 0.95f), ImGui::Style::with_alpha(ImGui::Style::lerp(base, hue, 0.40f + lift), 0.95f));
        draw->AddLine(ImVec2(min.x + 1.0f, header_max.y), ImVec2(max.x - 1.0f, header_max.y), ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::lerp(hue, ImVec4(1, 1, 1, 1), 0.3f), 0.45f)), 1.0f);

        ImGui::EditorUi::draw_lit_rim(draw, min, max, rounding, ImVec4(1.0f, 1.0f, 1.0f, 0.30f), ImVec4(1.0f, 1.0f, 1.0f, 0.0f), (max.y - min.y) * 0.6f);
        if (selected)
        {
            draw->AddRect(min, max, ImGui::EditorUi::color(ImGui::Style::color_accent_hi), rounding, 1.5f);
        }
        else if (alert)
        {
            draw->AddRect(min, max, ImGui::EditorUi::color(*alert), rounding, 1.5f);
        }
        else
        {
            draw->AddRect(min, max, ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, hovered ? 0.30f : 0.10f)), rounding, 1.0f);
        }
    }

    // the card at the top of the side panel, what the selected node is and what kind of node it is
    void draw_node_header_card(const char* kicker, const char* title, const char* subtitle, const ImVec4& hue)
    {
        ImDrawList* draw       = ImGui::GetWindowDrawList();
        const ImVec2 min       = ImGui::GetCursorScreenPos();
        const float width      = ImGui::GetContentRegionAvail().x;
        const float font_size  = ImGui::GetFontSize();
        const float micro      = ImGui::EditorUi::micro_label_size();
        const float title_size = font_size * 1.15f;
        const float padding    = ImGui::EditorUi::scaled(10.0f);
        const bool has_sub     = subtitle && subtitle[0];
        const float height     = padding * 2.0f + micro + 5.0f + title_size + (has_sub ? font_size * 0.9f + 4.0f : 0.0f);
        const ImVec2 max       = ImVec2(min.x + width, min.y + height);
        const float rounding   = ImGui::EditorUi::scaled(6.0f);

        const int vtx_start = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding);
        ImGui::EditorUi::shade_vertical(draw, vtx_start, min.y, max.y, ImGui::Style::lerp(ImGui::Style::color_panel, hue, 0.22f), ImGui::Style::lerp(ImGui::Style::color_panel, hue, 0.05f));
        draw->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), ImGui::EditorUi::color(hue), rounding, ImDrawFlags_RoundCornersLeft);
        ImGui::EditorUi::draw_lit_rim(draw, min, max, rounding, ImVec4(1, 1, 1, 0.16f), ImVec4(1, 1, 1, 0.0f), height * 0.6f);

        float y = min.y + padding;
        ImGui::EditorUi::draw_micro_label(draw, ImVec2(min.x + padding + 4.0f, y), micro, kicker, ImGui::Style::lerp(hue, ImVec4(1, 1, 1, 1), 0.45f));
        y += micro + 5.0f;
        draw->PushClipRect(min, ImVec2(max.x - padding, max.y), true);
        draw->AddText(Editor::font_bold, title_size, ImVec2(min.x + padding + 4.0f, y), ImGui::EditorUi::color(ImGui::Style::color_text), title);
        if (has_sub)
        {
            draw->AddText(nullptr, font_size * 0.9f, ImVec2(min.x + padding + 4.0f, y + title_size + 4.0f), ImGui::EditorUi::color(ImGui::Style::color_text_muted), subtitle);
        }
        draw->PopClipRect();
        ImGui::Dummy(ImVec2(width, height));
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(4.0f)));
    }

    // the panes the side panel and canvas sit in, rounded like every other tool window's
    bool begin_pane(const char* id, const ImVec2& size, ImGuiWindowFlags flags = 0)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
        const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, flags);
        ImGui::PopStyleVar();
        return open;
    }
}

bool VisualScriptEditor::PinKey::operator<(const PinKey& other) const
{
    if (node != other.node)
    {
        return node < other.node;
    }
    if (output != other.output)
    {
        return output < other.output;
    }
    return pin < other.pin;
}

VisualScriptEditor::VisualScriptEditor(Editor* editor) : Widget(editor)
{
    m_title         = "Visual Script";
    m_flags         = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    m_visible       = false;
    m_toolbar_order = 13;
    m_toolbar_icon  = static_cast<int>(IconType::Script);
    m_alpha         = 1.0f;
    m_size_initial  = spartan::math::Vector2(1400.0f, 800.0f);
}

// document

bool VisualScriptEditor::Open(const string& path, string* error)
{
    Graph graph;
    string load_error;
    if (!load_visual_script(path, graph, &load_error))
    {
        if (error)
        {
            *error = load_error.empty() ? "could not load " + path : load_error;
        }
        return false;
    }

    m_path  = path;
    m_graph = move(graph);
    m_dirty = false;
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_node_sizes.clear();
    m_change_open     = false;
    m_link_drag       = LinkDrag();
    m_add_menu_link   = LinkDrag();
    m_status          = "Opened " + FileSystem::GetFileNameFromFilePath(path);
    m_compile_pending = false;
    Compile();
    m_visible = true;
    return true;
}

bool VisualScriptEditor::Create(const string& path, string* error)
{
    if (FileSystem::Exists(path))
    {
        if (error)
        {
            *error = path + " already exists.";
        }
        return false;
    }
    if (!create_visual_script(path))
    {
        if (error)
        {
            *error = "could not write " + path + ".";
        }
        return false;
    }
    if (!Open(path, error))
    {
        return false;
    }
    m_frame_pending = true;
    return true;
}

CompileResult VisualScriptEditor::Save(uint32_t* reloaded_components)
{
    if (!IsOpen())
    {
        CompileResult result;
        Diagnostic diagnostic;
        diagnostic.message = "no visual script is open.";
        result.diagnostics.push_back(diagnostic);
        return result;
    }

    uint32_t reloaded = 0;
    m_compiled        = save_visual_script(m_path, m_graph, &reloaded);
    m_compile_pending = false;
    m_dirty           = false;
    if (reloaded_components)
    {
        *reloaded_components = reloaded;
    }

    size_t errors = 0;
    for (const Diagnostic& diagnostic : m_compiled.diagnostics)
    {
        errors += diagnostic.error ? 1 : 0;
    }
    m_status = "Saved" + string(reloaded ? ", reloaded " + to_string(reloaded) + " script component" + (reloaded == 1 ? "" : "s") : "");
    if (errors)
    {
        m_status += ", " + to_string(errors) + " error" + (errors == 1 ? "" : "s");
    }
    return m_compiled;
}

const CompileResult& VisualScriptEditor::Compile()
{
    m_compiled        = compile(m_graph);
    m_compile_pending = false;
    return m_compiled;
}

void VisualScriptEditor::BeginChange()
{
    if (!m_change_open)
    {
        m_change_before = m_graph.to_xml();
        m_change_open   = true;
    }
}

void VisualScriptEditor::EndChange()
{
    if (!m_change_open)
    {
        return;
    }
    m_change_open = false;

    if (m_graph.to_xml() == m_change_before)
    {
        return;
    }

    m_undo.push_back(move(m_change_before));
    if (m_undo.size() > k_undo_depth)
    {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    MarkChanged();
}

void VisualScriptEditor::MarkChanged()
{
    m_dirty           = true;
    m_compile_pending = true;
}

void VisualScriptEditor::Undo()
{
    if (m_undo.empty())
    {
        return;
    }
    m_redo.push_back(m_graph.to_xml());
    // the view is not part of the history, an undo should not jump the camera
    const float view_x = m_graph.view_x, view_y = m_graph.view_y, view_zoom = m_graph.view_zoom;
    m_graph.from_xml(m_undo.back());
    m_graph.view_x = view_x; m_graph.view_y = view_y; m_graph.view_zoom = view_zoom;
    m_undo.pop_back();
    m_selection.clear();
    MarkChanged();
}

void VisualScriptEditor::Redo()
{
    if (m_redo.empty())
    {
        return;
    }
    m_undo.push_back(m_graph.to_xml());
    const float view_x = m_graph.view_x, view_y = m_graph.view_y, view_zoom = m_graph.view_zoom;
    m_graph.from_xml(m_redo.back());
    m_graph.view_x = view_x; m_graph.view_y = view_y; m_graph.view_zoom = view_zoom;
    m_redo.pop_back();
    m_selection.clear();
    MarkChanged();
}

void VisualScriptEditor::FocusNode(uint32_t id)
{
    if (m_graph.find_node(id))
    {
        m_selection  = { id };
        m_focus_node = id;
    }
}

void VisualScriptEditor::FrameAll()
{
    m_frame_pending = true;
}

void VisualScriptEditor::GetFreePosition(float& x, float& y) const
{
    if (m_graph.nodes.empty())
    {
        x = 0.0f;
        y = 0.0f;
        return;
    }
    float right = -FLT_MAX;
    float top   = FLT_MAX;
    for (const Node& node : m_graph.nodes)
    {
        auto size = m_node_sizes.find(node.id);
        right     = max(right, node.x + (size != m_node_sizes.end() ? size->second.x : 220.0f));
        top       = min(top, node.y);
    }
    x = right + 60.0f;
    y = top;
}

bool VisualScriptEditor::AttachToSelectedEntity(string* error)
{
    return AttachToEntity(Selection::GetSelectedEntity(), error);
}

bool VisualScriptEditor::AttachToEntity(Entity* entity, string* error)
{
    if (!IsOpen())
    {
        if (error)
        {
            *error = "no visual script is open.";
        }
        return false;
    }
    if (!entity)
    {
        if (error)
        {
            *error = "select an entity first.";
        }
        return false;
    }

    if (m_dirty || !FileSystem::Exists(m_path))
    {
        Save();
    }

    Script* script = entity->GetComponent<Script>();
    if (!script)
    {
        script = entity->AddComponent<Script>();
    }
    script->LoadScriptFile(m_path);
    m_status = "Attached to " + entity->GetObjectName();
    return true;
}

// coordinates

ImVec2 VisualScriptEditor::ToScreen(float x, float y) const
{
    const float zoom = m_graph.view_zoom;
    return ImVec2(m_origin.x + (x + m_graph.view_x) * zoom, m_origin.y + (y + m_graph.view_y) * zoom);
}

ImVec2 VisualScriptEditor::ToCanvas(const ImVec2& screen) const
{
    const float zoom = m_graph.view_zoom;
    return ImVec2((screen.x - m_origin.x) / zoom - m_graph.view_x, (screen.y - m_origin.y) / zoom - m_graph.view_y);
}

uint32_t VisualScriptEditor::NodeColor(const NodeDefinition& definition) const
{
    switch (definition.kind)
    {
        case NodeKind::Event:
        case NodeKind::KeyEvent:
        case NodeKind::CustomEvent:     return color(236, 88, 74);
        case NodeKind::CallCustomEvent: return color(214, 118, 92);
        case NodeKind::Branch:
        case NodeKind::Sequence:
        case NodeKind::ForLoop:
        case NodeKind::ForEach:
        case NodeKind::WhileLoop:
        case NodeKind::DoOnce:
        case NodeKind::FlipFlop:
        case NodeKind::Delay:           return color(158, 168, 196);
        case NodeKind::VariableGet:
        case NodeKind::VariableSet:     return pin_color(definition.outputs.empty() ? "any" : definition.outputs.back().type);
        case NodeKind::Literal:         return color(118, 206, 118);
        case NodeKind::Cast:
        case NodeKind::Break:
        case NodeKind::GetComponent:    return color(64, 196, 206);
        case NodeKind::SelfEntity:      return color(74, 156, 255);
        case NodeKind::Comment:         return color(150, 150, 162);
        default:                        return definition.pure ? color(92, 212, 146) : color(66, 160, 255);
    }
}

string VisualScriptEditor::DiagnosticFor(uint32_t node, bool& error) const
{
    string text;
    error = false;
    for (const Diagnostic& diagnostic : m_compiled.diagnostics)
    {
        if (diagnostic.node == node)
        {
            text  += (text.empty() ? "" : "\n") + diagnostic.message;
            error  = error || diagnostic.error;
        }
    }
    return text;
}

// frame

void VisualScriptEditor::OnTickVisible()
{
    if (m_compile_pending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        Compile();
    }

    DrawToolbar();
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

    if (!IsOpen())
    {
        // an empty canvas that says what goes here and offers the two ways in
        begin_pane("##visual_script_empty", ImVec2(0.0f, 0.0f));
        const ImVec2 origin   = ImGui::GetWindowPos();
        const ImVec2 size     = ImGui::GetWindowSize();
        ImDrawList* draw      = ImGui::GetWindowDrawList();
        const char* title     = "No visual script is open";
        const char* body      = "Wire events to actions instead of writing Lua. A graph compiles to an ordinary script you can attach to any entity.";
        const float wrap      = std::min(size.x - ImGui::EditorUi::scaled(40.0f), ImGui::GetFontSize() * 30.0f);
        const ImVec2 title_sz = Editor::font_bold->CalcTextSizeA(ImGui::GetFontSize() * 1.3f, FLT_MAX, 0.0f, title);
        const ImVec2 body_sz  = ImGui::CalcTextSize(body, nullptr, false, wrap);
        const float button_w  = editor_ui::toolbar::ghost_button_width("Open...") + ImGui::CalcTextSize("New visual script").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::EditorUi::scaled(8.0f);
        float y               = origin.y + (size.y - title_sz.y - body_sz.y - ImGui::GetFrameHeight() - ImGui::EditorUi::scaled(28.0f)) * 0.5f;
        draw->AddText(Editor::font_bold, ImGui::GetFontSize() * 1.3f, ImVec2(IM_ROUND(origin.x + (size.x - title_sz.x) * 0.5f), IM_ROUND(y)), ImGui::EditorUi::color(ImGui::Style::color_text), title);
        y += title_sz.y + ImGui::EditorUi::scaled(8.0f);
        ImGui::SetCursorScreenPos(ImVec2(IM_ROUND(origin.x + (size.x - body_sz.x) * 0.5f), IM_ROUND(y)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", body);
        ImGui::PopTextWrapPos();
        y += body_sz.y + ImGui::EditorUi::scaled(20.0f);
        ImGui::SetCursorScreenPos(ImVec2(IM_ROUND(origin.x + (size.x - button_w) * 0.5f), IM_ROUND(y)));
        if (editor_ui::primary_button("New visual script"))
        {
            m_open_request = true;
        }
        ImGui::SameLine(0.0f, ImGui::EditorUi::scaled(8.0f));
        if (editor_ui::toolbar::ghost_button("Open...", "Pick a visual script found under the working directory or next to the world"))
        {
            m_open_request = true;
        }
        ImGui::EndChild();
        DrawOpenPopup();
        return;
    }

    const float side = m_show_side ? k_side_width * ImGui::GetStyle().FontSizeBase / 15.0f : 0.0f;
    if (m_show_side)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::EditorUi::scaled(ImVec2(10.0f, 10.0f)));
        begin_pane("##visual_script_side", ImVec2(side, 0.0f));
        ImGui::PopStyleVar();
        DrawSidePanel();
        ImGui::EndChild();
        ImGui::SameLine(0.0f, ImGui::EditorUi::scaled(6.0f));
    }

    DrawCanvas();
    DrawOpenPopup();
    DrawLuaWindow();
}

void VisualScriptEditor::DrawToolbar()
{
    namespace toolbar = editor_ui::toolbar;
    const float gap   = ImGui::EditorUi::scaled(6.0f);

    if (!IsOpen())
    {
        if (toolbar::ghost_button("New", "Create a visual script"))
        {
            m_open_request = true;
        }
        ImGui::SameLine(0.0f, gap);
        if (toolbar::ghost_button("Open", "Open a visual script found under the working directory or next to the world"))
        {
            m_open_request = true;
        }
        return;
    }

    // saving is what this window exists for, so it is the only filled button, amber while the graph has unsaved edits
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_Tooltip);
    if (m_dirty ? editor_ui::attention_button("Save", true) : editor_ui::primary_button("Save"))
    {
        Save();
    }
    toolbar::tooltip("Compiles the graph to lua, writes the file and reloads every script component running it");
    ImGui::SameLine(0.0f, gap);
    if (toolbar::ghost_button("Attach to selected", "Run this script on the entity selected in the world, a script component is added when it has none"))
    {
        string error;
        if (!AttachToSelectedEntity(&error))
        {
            m_status = error;
        }
    }

    toolbar::divider();
    if (toolbar::ghost_button("New", "Create a visual script"))
    {
        m_open_request = true;
    }
    ImGui::SameLine(0.0f, gap);
    if (toolbar::ghost_button("Open", "Open a visual script found under the working directory or next to the world"))
    {
        m_open_request = true;
    }

    toolbar::divider();
    ImGui::BeginDisabled(m_undo.empty());
    if (toolbar::ghost_button("Undo", "Ctrl+Z"))
    {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginDisabled(m_redo.empty());
    if (toolbar::ghost_button("Redo", "Ctrl+Y"))
    {
        Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    if (toolbar::ghost_button("Frame", "Fit the graph, or the selection, in view (F)"))
    {
        FrameAll();
    }

    toolbar::divider();
    if (toolbar::pill("Panel", m_show_side, ImGui::Style::color_accent_1, "Variables, the selected node and problems"))
    {
        m_show_side = !m_show_side;
    }
    ImGui::SameLine(0.0f, gap);
    if (toolbar::pill("Lua", m_show_lua, ImGui::Style::color_accent_1, "The script the graph compiles to"))
    {
        m_show_lua = !m_show_lua;
    }

    // the file and its health on the right, a problem is a pill that opens the panel where it is listed
    size_t errors = 0, warnings = 0;
    for (const Diagnostic& diagnostic : m_compiled.diagnostics)
    {
        (diagnostic.error ? errors : warnings)++;
    }
    char health[64];
    ImVec4 health_tint = ImGui::Style::color_ok;
    if (errors)
    {
        snprintf(health, sizeof(health), "%zu error%s###health", errors, errors == 1 ? "" : "s");
        health_tint = ImGui::Style::color_error;
    }
    else if (warnings)
    {
        snprintf(health, sizeof(health), "%zu warning%s###health", warnings, warnings == 1 ? "" : "s");
        health_tint = ImGui::Style::color_warning;
    }
    else
    {
        snprintf(health, sizeof(health), "Compiles###health");
    }
    const string file    = FileSystem::GetFileNameFromFilePath(m_path) + (m_dirty ? "  \xC2\xB7  modified" : "");
    const string& status = m_status;
    const float file_w   = ImGui::CalcTextSize(file.c_str()).x;
    const float status_w = status.empty() ? 0.0f : ImGui::CalcTextSize(status.c_str()).x + ImGui::EditorUi::scaled(16.0f);
    toolbar::align_right(status_w + file_w + ImGui::EditorUi::scaled(12.0f) + toolbar::pill_width(health, true));
    if (!status.empty())
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::Style::color_text_faint, "%s", status.c_str());
        ImGui::SameLine(0.0f, ImGui::EditorUi::scaled(16.0f));
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(m_dirty ? ImGui::Style::color_warning : ImGui::Style::color_text_muted, "%s", file.c_str());
    toolbar::tooltip(m_path.c_str());
    ImGui::SameLine(0.0f, ImGui::EditorUi::scaled(12.0f));
    if (toolbar::pill(health, errors || warnings, health_tint, errors || warnings ? "Show the problems in the panel" : "The graph compiles to lua without problems", true, errors > 0))
    {
        m_show_side = true;
    }
}

void VisualScriptEditor::DrawSidePanel()
{
    // the selected node, the place to name a custom event or write a comment
    if (m_selection.size() == 1)
    {
        Node* node = m_graph.find_node(*m_selection.begin());
        NodeDefinition definition;
        if (node && resolve_definition(node->type, m_graph, definition))
        {
            const string category = definition.category.empty() ? string("Node") : definition.category.substr(0, definition.category.find('|'));
            const string subtitle = definition.id + "  \xC2\xB7  node " + to_string(node->id);
            draw_node_header_card(category.c_str(), definition.title.c_str(), subtitle.c_str(), to_vec4(NodeColor(definition)));
            if (!definition.tooltip.empty())
            {
                editor_ui::layout::caption(definition.tooltip.c_str());
            }

            if (definition.kind == NodeKind::CustomEvent || definition.kind == NodeKind::Comment)
            {
                const char* key = definition.kind == NodeKind::Comment ? "text" : "name";
                string value    = node->data[key];
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool edited = definition.kind == NodeKind::Comment ? ImGui::InputTextMultiline("##data", &value, ImVec2(-FLT_MIN, 80.0f)) : ImGui::InputText("##data", &value);
                ImGui::EditorUi::decorate_field();
                if (ImGui::IsItemActivated())
                {
                    BeginChange();
                }
                if (edited)
                {
                    node->data[key] = value;
                }
                if (ImGui::IsItemDeactivated())
                {
                    EndChange();
                }
            }

            for (const PinDefinition& pin : definition.inputs)
            {
                if (!has_inline_value(pin) || (pin.connectable && m_graph.is_connected(node->id, pin.name, false)))
                {
                    continue;
                }
                ImGui::TextUnformatted(pin.name.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImGui::Style::color_text_faint, "%s", pin.type.c_str());
                DrawInlineValue(*node, pin, ImGui::GetContentRegionAvail().x);
            }

            bool error         = false;
            const string notes = DiagnosticFor(node->id, error);
            if (!notes.empty())
            {
                editor_ui::layout::note(notes.c_str(), error ? ImGui::Style::color_error : ImGui::Style::color_warning);
            }
            ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(8.0f)));
        }
    }

    DrawVariables();
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(8.0f)));
    DrawDiagnostics();
}

void VisualScriptEditor::DrawVariables()
{
    const string count = to_string(m_graph.variables.size());
    if (!editor_ui::layout::fold("Variables", true, count.c_str()))
    {
        return;
    }
    editor_ui::layout::caption("Numbers, booleans and strings show up in the entity's properties and are saved with it.");
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
    if (editor_ui::toolbar::ghost_button("+ Add variable", "A new number, change its type next to its name", ImGui::Style::color_accent_1))
    {
        string name = "value";
        for (uint32_t i = 2; m_graph.find_variable(name); i++)
        {
            name = "value" + to_string(i);
        }
        BeginChange();
        add_variable(m_graph, name, "number", "0");
        EndChange();
    }

    const vector<string> types = get_variable_types();
    string remove;
    for (size_t i = 0; i < m_graph.variables.size(); i++)
    {
        Variable& variable = m_graph.variables[i];
        ImGui::PushID(static_cast<int>(i));

        // a rename commits when the field loses focus, every keystroke would rewire the graph
        string name = variable.name;
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(4.0f)));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::InputText("##name", &name);
        ImGui::EditorUi::decorate_field();
        if (ImGui::IsItemDeactivatedAfterEdit() && name != variable.name)
        {
            string error;
            BeginChange();
            if (!rename_variable(m_graph, variable.name, name, &error))
            {
                m_status = error;
            }
            EndChange();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##type", m_graph.variables[i].type.c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (const string& type : types)
            {
                if (ImGui::Selectable(type.c_str(), type == m_graph.variables[i].type))
                {
                    BeginChange();
                    set_variable_type(m_graph, m_graph.variables[i].name, type);
                    EndChange();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EditorUi::decorate_field();
        ImGui::SameLine();
        if (editor_ui::toolbar::ghost_button("x", "Remove the variable and every node reading or writing it", ImGui::Style::color_error, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
        {
            remove = m_graph.variables[i].name;
        }

        Variable& current = m_graph.variables[i];
        PinDefinition pin;
        pin.name          = current.name;
        pin.type          = current.type;
        pin.default_value = default_value_for(current.type);
        if (has_inline_value(pin))
        {
            Node holder;
            holder.values[pin.name] = current.value;
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::Style::color_text_faint, "starts as");
            ImGui::SameLine();
            if (DrawInlineValue(holder, pin, ImGui::GetContentRegionAvail().x))
            {
                current.value = holder.values[pin.name];
            }
        }

        if (editor_ui::toolbar::ghost_button("Get", "Add a node that reads it", ImGui::Style::color_accent_1))
        {
            const ImVec2 center = ToCanvas(ImVec2(m_origin.x + m_canvas_size.x * 0.5f, m_origin.y + m_canvas_size.y * 0.5f));
            BeginChange();
            if (Node* node = add_node(m_graph, "var.get:" + current.name, center.x, center.y))
            {
                m_selection = { node->id };
            }
            EndChange();
        }
        ImGui::SameLine(0.0f, ImGui::EditorUi::scaled(4.0f));
        if (editor_ui::toolbar::ghost_button("Set", "Add a node that writes it", ImGui::Style::color_accent_1))
        {
            const ImVec2 center = ToCanvas(ImVec2(m_origin.x + m_canvas_size.x * 0.5f, m_origin.y + m_canvas_size.y * 0.5f));
            BeginChange();
            if (Node* node = add_node(m_graph, "var.set:" + current.name, center.x, center.y))
            {
                m_selection = { node->id };
            }
            EndChange();
        }
        editor_ui::layout::separator();
        ImGui::PopID();
    }

    if (!remove.empty())
    {
        BeginChange();
        remove_variable(m_graph, remove);
        EndChange();
        m_selection.clear();
    }
}

void VisualScriptEditor::DrawDiagnostics()
{
    const string count = to_string(m_compiled.diagnostics.size());
    if (!editor_ui::layout::fold("Problems", true, count.c_str()))
    {
        return;
    }
    if (m_compiled.diagnostics.empty())
    {
        editor_ui::layout::note("None, the graph compiles.", ImGui::Style::color_ok);
        return;
    }

    // a problem that belongs to a node is a row that takes you to it
    for (size_t i = 0; i < m_compiled.diagnostics.size(); i++)
    {
        const Diagnostic& diagnostic = m_compiled.diagnostics[i];
        const ImVec4 tint            = diagnostic.error ? ImGui::Style::color_error : ImGui::Style::color_warning;
        const string label           = (diagnostic.node ? "Node " + to_string(diagnostic.node) + ": " : string()) + diagnostic.message;
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        editor_ui::layout::note(label.c_str(), tint);
        const ImVec2 end   = ImVec2(start.x + ImGui::GetContentRegionAvail().x, ImGui::GetCursorScreenPos().y);
        const bool hovered = diagnostic.node && ImGui::IsMouseHoveringRect(start, end) && ImGui::IsWindowHovered();
        ImGui::EditorUi::draw_row_highlight(start, end, hovered, false);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            FocusNode(diagnostic.node);
        }
        ImGui::PopID();
    }
}

bool VisualScriptEditor::DrawInlineValue(Node& node, const PinDefinition& pin, float width)
{
    auto it            = node.values.find(pin.name);
    const string value = it != node.values.end() ? it->second : pin.default_value;
    string edited      = value;
    bool changed       = false;
    const string& type = pin.type;

    ImGui::PushID(pin.name.c_str());
    ImGui::SetNextItemWidth(width);

    if (const ApiEnum* entry = find_enum(type))
    {
        // a combo commits on pick, it has no drag to bracket
        if (ImGui::BeginCombo("##value", value.c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (const auto& option : entry->values)
            {
                if (ImGui::Selectable(option.first.c_str(), option.first == value))
                {
                    BeginChange();
                    node.values[pin.name] = option.first;
                    EndChange();
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    if (type == "boolean")
    {
        bool flag = value == "true" || value == "1";
        if (ImGui::Checkbox("##value", &flag))
        {
            edited  = flag ? "true" : "false";
            changed = true;
        }
    }
    else if (type == "integer")
    {
        int number = static_cast<int>(strtol(value.c_str(), nullptr, 10));
        if (ImGui::DragInt("##value", &number, 0.1f))
        {
            edited  = to_string(number);
            changed = true;
        }
    }
    else if (type == "number")
    {
        float number = strtof(value.c_str(), nullptr);
        if (ImGui::DragFloat("##value", &number, 0.01f, 0.0f, 0.0f, "%.6g"))
        {
            edited  = number_text(number);
            changed = true;
        }
    }
    else if (const size_t count = vector_size(type))
    {
        vector<float> numbers = parse_floats(value, count);
        if (ImGui::DragScalarN("##value", ImGuiDataType_Float, numbers.data(), static_cast<int>(count), 0.01f, nullptr, nullptr, "%.6g"))
        {
            edited.clear();
            for (size_t i = 0; i < count; i++)
            {
                edited += (i ? ", " : "") + number_text(numbers[i]);
            }
            changed = true;
        }
    }
    else
    {
        changed = ImGui::InputText("##value", &edited);
    }
    ImGui::EditorUi::decorate_field();

    if (ImGui::IsItemActivated())
    {
        BeginChange();
    }
    if (changed)
    {
        node.values[pin.name] = edited;
    }
    if (ImGui::IsItemDeactivated())
    {
        EndChange();
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
    {
        ImGui::SetTooltip("%s (%s)", pin.name.c_str(), type.c_str());
    }

    ImGui::PopID();
    return changed;
}

// canvas

void VisualScriptEditor::DrawGrid(ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    // smoked over the frosted panel instead of painted over it, the scene stays faintly readable behind the graph
    const float rounding = ImGui::EditorUi::scaled(6.0f);
    draw->AddRectFilled(min, max, ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_canvas_deep, 0.62f)), rounding);

    const float step = k_grid * m_graph.view_zoom;
    if (step >= 6.0f)
    {
        const ImU32 minor    = ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.030f));
        const ImU32 major    = ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.065f));
        const float offset_x = fmodf(m_graph.view_x * m_graph.view_zoom, step);
        const float offset_y = fmodf(m_graph.view_y * m_graph.view_zoom, step);
        int index_x          = static_cast<int>(floorf(-m_graph.view_x / k_grid));
        for (float x = min.x + offset_x - (offset_x > 0 ? step : 0.0f); x < max.x; x += step, index_x++)
        {
            draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), (index_x % 4 == 0) ? major : minor);
        }
        int index_y = static_cast<int>(floorf(-m_graph.view_y / k_grid));
        for (float y = min.y + offset_y - (offset_y > 0 ? step : 0.0f); y < max.y; y += step, index_y++)
        {
            draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), (index_y % 4 == 0) ? major : minor);
        }
    }

    // light falling on the top of the pane, like the sheen on every glass panel
    const int sheen_vtx = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, ImVec2(max.x, min.y + (max.y - min.y) * 0.35f), IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersTop);
    ImGui::EditorUi::shade_vertical(draw, sheen_vtx, min.y, min.y + (max.y - min.y) * 0.35f, ImVec4(1.0f, 1.0f, 1.0f, 0.035f), ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
}

void VisualScriptEditor::DrawCanvas()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    begin_pane("##visual_script_canvas", ImVec2(0.0f, 0.0f), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    m_origin      = ImGui::GetCursorScreenPos();
    m_canvas_size = ImGui::GetContentRegionAvail();
    const ImVec2 canvas_max = add(m_origin, m_canvas_size);
    ImDrawList* draw        = ImGui::GetWindowDrawList();
    const bool focused      = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    m_canvas_hovered        = ImGui::IsWindowHovered();

    if (focused)
    {
        ImGuiSp::editor_shortcuts_claimed_frame = ImGui::GetFrameCount();
    }

    // framing waits for node sizes, which the first frame of a fresh graph measures
    if (m_frame_pending && m_canvas_size.x > 1.0f)
    {
        m_frame_pending = false;
        float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
        for (const Node& node : m_graph.nodes)
        {
            if (!m_selection.empty() && !m_selection.count(node.id) && m_selection.size() > 1)
            {
                continue;
            }
            auto size = m_node_sizes.find(node.id);
            const ImVec2 extent = size != m_node_sizes.end() ? size->second : ImVec2(200.0f, 100.0f);
            min_x = min(min_x, node.x);
            min_y = min(min_y, node.y);
            max_x = max(max_x, node.x + extent.x);
            max_y = max(max_y, node.y + extent.y);
        }
        if (min_x <= max_x)
        {
            const float margin   = 80.0f;
            m_graph.view_zoom    = clamp(min(m_canvas_size.x / (max_x - min_x + margin * 2.0f), m_canvas_size.y / (max_y - min_y + margin * 2.0f)), k_zoom_min, 1.0f);
            m_graph.view_x       = m_canvas_size.x / m_graph.view_zoom * 0.5f - (min_x + max_x) * 0.5f;
            m_graph.view_y       = m_canvas_size.y / m_graph.view_zoom * 0.5f - (min_y + max_y) * 0.5f;
        }
    }
    if (m_focus_node)
    {
        if (const Node* node = m_graph.find_node(m_focus_node))
        {
            auto size = m_node_sizes.find(node->id);
            const ImVec2 extent = size != m_node_sizes.end() ? size->second : ImVec2(200.0f, 100.0f);
            m_graph.view_zoom   = max(m_graph.view_zoom, 0.8f);
            m_graph.view_x      = m_canvas_size.x / m_graph.view_zoom * 0.5f - (node->x + extent.x * 0.5f);
            m_graph.view_y      = m_canvas_size.y / m_graph.view_zoom * 0.5f - (node->y + extent.y * 0.5f);
        }
        m_focus_node = 0;
    }

    DrawGrid(draw, m_origin, canvas_max);

    // catches clicks on empty space so they pan, box select and open the menu instead of dragging the window
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas_background", m_canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    // pan with the middle or right button, zoom around the cursor
    if (m_canvas_hovered || ImGui::IsItemActive())
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.0f))
        {
            m_graph.view_x += io.MouseDelta.x / m_graph.view_zoom;
            m_graph.view_y += io.MouseDelta.y / m_graph.view_zoom;
        }
    }
    if (m_canvas_hovered && io.MouseWheel != 0.0f)
    {
        const ImVec2 anchor = ToCanvas(io.MousePos);
        m_graph.view_zoom   = clamp(m_graph.view_zoom * powf(1.12f, io.MouseWheel), k_zoom_min, k_zoom_max);
        m_graph.view_x      = (io.MousePos.x - m_origin.x) / m_graph.view_zoom - anchor.x;
        m_graph.view_y      = (io.MousePos.y - m_origin.y) / m_graph.view_zoom - anchor.y;
    }

    // what is under the cursor, from the sizes measured last frame
    uint32_t node_under_mouse = 0;
    for (auto it = m_graph.nodes.rbegin(); it != m_graph.nodes.rend(); ++it)
    {
        auto size = m_node_sizes.find(it->id);
        if (size == m_node_sizes.end())
        {
            continue;
        }
        const ImVec2 min = ToScreen(it->x, it->y);
        const bool comment = it->type == "util.comment";
        const ImVec2 max = add(min, mul(ImVec2(size->second.x, comment ? k_header : size->second.y), m_graph.view_zoom));
        if (io.MousePos.x >= min.x && io.MousePos.x <= max.x && io.MousePos.y >= min.y && io.MousePos.y <= max.y)
        {
            node_under_mouse = it->id;
            break;
        }
    }

    // box selection starts on empty space
    if (m_canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !node_under_mouse && !m_pin_hovered && !m_link_drag.active)
    {
        if (!io.KeyCtrl && !io.KeyShift)
        {
            m_selection.clear();
        }
        m_box_selecting = true;
        m_box_start     = io.MousePos;
    }

    // layers: comments, then links, then nodes
    ImDrawListSplitter splitter;
    splitter.Split(draw, 3);
    m_pin_positions.clear();
    m_pin_types.clear();

    const float base_font = ImGui::GetStyle().FontSizeBase;
    ImGui::PushFont(nullptr, base_font * m_graph.view_zoom);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * m_graph.view_zoom, 2.0f * m_graph.view_zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(3.0f * m_graph.view_zoom, 3.0f * m_graph.view_zoom));

    m_bring_to_front = 0;
    for (size_t i = 0; i < m_graph.nodes.size(); i++)
    {
        if (m_graph.nodes[i].type == "util.comment")
        {
            splitter.SetCurrentChannel(draw, 0);
            DrawComment(draw, m_graph.nodes[i]);
        }
    }
    for (size_t i = 0; i < m_graph.nodes.size(); i++)
    {
        Node& node = m_graph.nodes[i];
        if (node.type == "util.comment")
        {
            continue;
        }
        NodeDefinition definition;
        if (!resolve_definition(node.type, m_graph, definition))
        {
            definition.id    = node.type;
            definition.title = "Unknown: " + node.type;
            definition.kind  = NodeKind::ApiCall;
        }
        splitter.SetCurrentChannel(draw, 2);
        DrawNode(draw, node, definition);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopFont();

    // the pin under the cursor, by distance so it works while another item holds the mouse
    m_pin_hovered = false;
    float best    = (k_row * 0.6f * m_graph.view_zoom) * (k_row * 0.6f * m_graph.view_zoom);
    for (const auto& [key, position] : m_pin_positions)
    {
        const ImVec2 delta = sub(io.MousePos, position);
        const float distance = delta.x * delta.x + delta.y * delta.y;
        if (distance < best)
        {
            best          = distance;
            m_hovered_pin = key;
            m_pin_hovered = true;
        }
    }

    splitter.SetCurrentChannel(draw, 1);
    DrawLinks(draw);

    // box
    if (m_box_selecting)
    {
        splitter.SetCurrentChannel(draw, 2);
        const ImVec2 a(min(m_box_start.x, io.MousePos.x), min(m_box_start.y, io.MousePos.y));
        const ImVec2 b(max(m_box_start.x, io.MousePos.x), max(m_box_start.y, io.MousePos.y));
        draw->AddRectFilled(a, b, ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_accent_1, 0.08f)), 3.0f);
        ImGui::EditorUi::corner_brackets(draw, a, b, std::min(12.0f, std::min(b.x - a.x, b.y - a.y) * 0.5f), 1.5f, ImGui::EditorUi::color(ImGui::Style::color_accent_hi));
        draw->AddRect(a, b, ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_accent_1, 0.35f)), 3.0f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_box_selecting = false;
            const ImVec2 ca = ToCanvas(a), cb = ToCanvas(b);
            for (const Node& node : m_graph.nodes)
            {
                auto size = m_node_sizes.find(node.id);
                const ImVec2 extent = size != m_node_sizes.end() ? size->second : ImVec2(100.0f, 60.0f);
                const bool comment  = node.type == "util.comment";
                // comments only when fully inside, they are big enough to be caught by accident
                const bool hit = comment ?
                    (node.x >= ca.x && node.y >= ca.y && node.x + extent.x <= cb.x && node.y + extent.y <= cb.y) :
                    (node.x <= cb.x && node.x + extent.x >= ca.x && node.y <= cb.y && node.y + extent.y >= ca.y);
                if (hit)
                {
                    m_selection.insert(node.id);
                }
            }
        }
    }

    splitter.Merge(draw);

    if (m_link_drag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        FinishLinkDrag();
    }

    if (m_nodes_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_nodes_dragging = false;
        EndChange();
    }

    // node to the top of the stack after a click, outside the loop that draws them
    if (m_bring_to_front)
    {
        auto it = find_if(m_graph.nodes.begin(), m_graph.nodes.end(), [this](const Node& node) { return node.id == m_bring_to_front; });
        if (it != m_graph.nodes.end() && it + 1 != m_graph.nodes.end())
        {
            Node node = *it;
            m_graph.nodes.erase(it);
            m_graph.nodes.push_back(node);
        }
    }

    // a short right click is a menu, a long one was a pan
    if (m_canvas_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f)
    {
        if (node_under_mouse)
        {
            if (!m_selection.count(node_under_mouse))
            {
                m_selection = { node_under_mouse };
            }
            ImGui::OpenPopup("##node_menu");
        }
        else if (m_pin_hovered)
        {
            m_menu_pin = m_hovered_pin;
            ImGui::OpenPopup("##pin_menu");
        }
        else
        {
            m_add_menu_request = true;
            m_add_menu_canvas  = ToCanvas(io.MousePos);
            m_add_menu_link    = LinkDrag();
        }
    }

    if (focused)
    {
        HandleShortcuts();
    }

    if (ImGui::BeginPopup("##node_menu"))
    {
        if (ImGui::MenuItem("Delete", "Del"))
        {
            DeleteSelection();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            DuplicateSelection();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C"))
        {
            CopySelection();
        }
        if (ImGui::MenuItem("Break all links"))
        {
            BeginChange();
            m_graph.links.erase(remove_if(m_graph.links.begin(), m_graph.links.end(), [this](const Link& link)
            {
                return m_selection.count(link.from_node) || m_selection.count(link.to_node);
            }), m_graph.links.end());
            EndChange();
        }
        if (ImGui::MenuItem("Comment around selection"))
        {
            float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
            for (const Node& node : m_graph.nodes)
            {
                if (m_selection.count(node.id))
                {
                    auto size = m_node_sizes.find(node.id);
                    const ImVec2 extent = size != m_node_sizes.end() ? size->second : ImVec2(200.0f, 100.0f);
                    min_x = min(min_x, node.x);
                    min_y = min(min_y, node.y);
                    max_x = max(max_x, node.x + extent.x);
                    max_y = max(max_y, node.y + extent.y);
                }
            }
            BeginChange();
            if (Node* comment = add_node(m_graph, "util.comment", min_x - 20.0f, min_y - k_header - 20.0f))
            {
                comment->data["w"] = number_text(max_x - min_x + 40.0f);
                comment->data["h"] = number_text(max_y - min_y + k_header + 40.0f);
                const uint32_t id  = comment->id;
                // comments sit behind everything
                auto it = find_if(m_graph.nodes.begin(), m_graph.nodes.end(), [id](const Node& node) { return node.id == id; });
                Node node = *it;
                m_graph.nodes.erase(it);
                m_graph.nodes.insert(m_graph.nodes.begin(), node);
                m_selection = { id };
            }
            EndChange();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##pin_menu"))
    {
        if (ImGui::MenuItem("Break links"))
        {
            BeginChange();
            m_graph.disconnect(m_menu_pin.node, m_menu_pin.pin, m_menu_pin.output);
            EndChange();
        }
        ImGui::EndPopup();
    }

    DrawAddMenu();

    // hint for an empty graph
    if (m_graph.nodes.empty())
    {
        const char* hint  = "Right click or press Tab to add a node";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        const float kick  = ImGui::EditorUi::micro_label_width("empty graph");
        const ImVec2 at   = ImVec2(IM_ROUND(m_origin.x + (m_canvas_size.x - size.x) * 0.5f), IM_ROUND(m_origin.y + (m_canvas_size.y - size.y) * 0.5f));
        ImGui::EditorUi::draw_micro_label(draw, ImVec2(IM_ROUND(m_origin.x + (m_canvas_size.x - kick) * 0.5f), at.y - ImGui::GetFontSize() * 1.6f), ImGui::GetFontSize(), "empty graph", ImGui::Style::color_text_faint);
        draw->AddText(at, ImGui::EditorUi::color(ImGui::Style::color_text_muted), hint);
    }

    ImGui::EndChild();
}

void VisualScriptEditor::DrawComment(ImDrawList* draw, Node& node)
{
    ImGuiIO& io        = ImGui::GetIO();
    const float zoom   = m_graph.view_zoom;
    const float width  = comment_size(node, "w", k_comment_w);
    const float height = comment_size(node, "h", k_comment_h);
    m_node_sizes[node.id] = ImVec2(width, height);

    const ImVec2 min      = ToScreen(node.x, node.y);
    const ImVec2 max      = add(min, mul(ImVec2(width, height), zoom));
    const bool selected   = m_selection.count(node.id) != 0;
    const float rounding  = 8.0f * zoom;
    const float header    = k_header * zoom;
    const ImVec4 hue      = ImGui::Style::color_accent_1;

    // a frame of faintly tinted glass behind a group of nodes, it reads as a region rather than another node
    if (selected)
    {
        ImGui::EditorUi::draw_glow(draw, min, max, hue, rounding, 10.0f * zoom, 0.8f);
    }
    draw->AddRectFilled(min, max, ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.035f)), rounding);
    const int header_vtx = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, ImVec2(max.x, min.y + header), IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersTop);
    ImGui::EditorUi::shade_vertical(draw, header_vtx, min.y, min.y + header, ImGui::Style::with_alpha(ImGui::Style::color_text, 0.11f), ImGui::Style::with_alpha(ImGui::Style::color_text, 0.05f));
    ImGui::EditorUi::draw_lit_rim(draw, min, max, rounding, ImVec4(1.0f, 1.0f, 1.0f, 0.20f), ImVec4(1.0f, 1.0f, 1.0f, 0.04f), max.y - min.y);
    if (selected)
    {
        draw->AddRect(min, max, ImGui::EditorUi::color(ImGui::Style::color_accent_hi), rounding, 1.5f);
    }

    // the first line is the title in the header, the rest wraps inside the frame as the note
    auto text = node.data.find("text");
    const string label     = text != node.data.end() ? text->second : "Comment";
    const size_t title_end = label.find('\n');
    draw->PushClipRect(min, ImVec2(max.x, min.y + header), true);
    draw->AddText(Editor::font_bold, ImGui::GetFontSize(), ImVec2(min.x + k_pad * zoom, min.y + (header - ImGui::GetFontSize()) * 0.5f), ImGui::EditorUi::color(ImGui::Style::color_text), label.c_str(), label.c_str() + std::min(title_end, label.size()));
    draw->PopClipRect();
    if (title_end != string::npos && title_end + 1 < label.size())
    {
        const float pad  = k_pad * zoom;
        const ImVec4 clip(min.x + pad, min.y + header, max.x - pad, max.y - pad);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(clip.x, min.y + header + pad * 0.6f), ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.72f)), label.c_str() + title_end + 1, nullptr, clip.z - clip.x, &clip);
    }

    ImGui::PushID(static_cast<int>(node.id));

    // the header drags it, along with the nodes inside
    ImGui::SetCursorScreenPos(min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##comment", ImVec2(max.x - min.x, k_header * zoom));
    HandleNodeInteraction(node, ImGui::IsItemClicked(), ImGui::IsItemActive(), ImGui::IsItemActivated(), ImGui::IsItemDeactivated());
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_selection = { node.id };
        m_show_side = true;
    }

    // corner resize
    const float grip = 14.0f * zoom;
    ImGui::SetCursorScreenPos(ImVec2(max.x - grip, max.y - grip));
    ImGui::InvisibleButton("##resize", ImVec2(grip, grip));
    const ImU32 grip_tint = ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, ImGui::IsItemHovered() || ImGui::IsItemActive() ? 0.55f : 0.22f));
    draw->AddLine(ImVec2(max.x - grip * 0.75f, max.y - 3.0f * zoom), ImVec2(max.x - 3.0f * zoom, max.y - grip * 0.75f), grip_tint, 1.5f);
    draw->AddLine(ImVec2(max.x - grip * 0.40f, max.y - 3.0f * zoom), ImVec2(max.x - 3.0f * zoom, max.y - grip * 0.40f), grip_tint, 1.5f);
    if (ImGui::IsItemActivated())
    {
        BeginChange();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        node.data["w"] = number_text(std::max(80.0f, width + io.MouseDelta.x / zoom));
        node.data["h"] = number_text(std::max(80.0f, height + io.MouseDelta.y / zoom));
    }
    if (ImGui::IsItemDeactivated())
    {
        EndChange();
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    }

    ImGui::PopID();
}

void VisualScriptEditor::HandleNodeInteraction(Node& node, bool clicked, bool active, bool activated, bool deactivated)
{
    ImGuiIO& io = ImGui::GetIO();
    (void)clicked;
    (void)deactivated;

    if (activated)
    {
        if (io.KeyCtrl || io.KeyShift)
        {
            if (m_selection.count(node.id) && io.KeyCtrl)
            {
                m_selection.erase(node.id);
            }
            else
            {
                m_selection.insert(node.id);
            }
        }
        else if (!m_selection.count(node.id))
        {
            m_selection = { node.id };
        }
        if (node.type != "util.comment")
        {
            m_bring_to_front = node.id;
        }
    }

    if (active && m_selection.count(node.id) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !m_link_drag.active)
    {
        if (!m_nodes_dragging)
        {
            m_nodes_dragging = true;
            BeginChange();
        }
        const float dx = io.MouseDelta.x / m_graph.view_zoom;
        const float dy = io.MouseDelta.y / m_graph.view_zoom;

        // a comment carries the nodes inside it
        set<uint32_t> moving = m_selection;
        for (const Node& comment : m_graph.nodes)
        {
            if (comment.type != "util.comment" || !m_selection.count(comment.id))
            {
                continue;
            }
            const float w = comment_size(comment, "w", k_comment_w);
            const float h = comment_size(comment, "h", k_comment_h);
            for (const Node& other : m_graph.nodes)
            {
                auto size = m_node_sizes.find(other.id);
                const ImVec2 extent = size != m_node_sizes.end() ? size->second : ImVec2(0.0f, 0.0f);
                if (other.id != comment.id && other.x >= comment.x && other.y >= comment.y && other.x + extent.x <= comment.x + w && other.y + extent.y <= comment.y + h)
                {
                    moving.insert(other.id);
                }
            }
        }
        for (Node& other : m_graph.nodes)
        {
            if (moving.count(other.id))
            {
                other.x += dx;
                other.y += dy;
            }
        }
    }
}

void VisualScriptEditor::DrawNode(ImDrawList* draw, Node& node, const NodeDefinition& definition)
{
    ImGuiIO& io         = ImGui::GetIO();
    const float zoom    = m_graph.view_zoom;
    const float header  = k_header * zoom;
    const float row     = k_row * zoom;
    const float pad     = k_pad * zoom;
    const float radius  = k_pin_radius * zoom;
    const bool widgets  = zoom >= 0.5f;

    // what each input shows: a label, and an inline editor while nothing feeds it
    auto shows_value = [&](const PinDefinition& pin)
    {
        return has_inline_value(pin) && (!pin.connectable || !m_graph.is_connected(node.id, pin.name, false));
    };

    string title = definition.title;
    if (definition.kind == NodeKind::CustomEvent)
    {
        auto name = node.data.find("name");
        title     = "Event " + (name != node.data.end() ? name->second : string("?"));
    }

    float left = 0.0f;
    for (const PinDefinition& pin : definition.inputs)
    {
        float width = pin_label_visible(pin, false) ? ImGui::CalcTextSize(pin.name.c_str()).x : 0.0f;
        if (shows_value(pin))
        {
            width += (width > 0.0f ? pad * 0.5f : 0.0f) + inline_width(pin.type) * zoom;
        }
        left = max(left, width);
    }
    float right = 0.0f;
    for (const PinDefinition& pin : definition.outputs)
    {
        right = max(right, pin_label_visible(pin, true) ? ImGui::CalcTextSize(pin.name.c_str()).x : 0.0f);
    }

    const bool is_event = definition.kind == NodeKind::Event || definition.kind == NodeKind::KeyEvent || definition.kind == NodeKind::CustomEvent;
    const float title_w = Editor::font_bold->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, title.c_str()).x + (is_event ? 6.0f * zoom + pad * 0.6f : 0.0f);
    const size_t rows   = max(definition.inputs.size(), definition.outputs.size());
    const float width   = max({ k_min_width * zoom, title_w + pad * 2.0f, radius * 2.0f + pad + left + pad * 2.0f + right + pad + radius * 2.0f });
    const float height = header + static_cast<float>(rows) * row + pad * 0.6f;
    m_node_sizes[node.id] = ImVec2(width / zoom, height / zoom);

    const ImVec2 min = ToScreen(node.x, node.y);
    const ImVec2 max = ImVec2(min.x + width, min.y + height);

    // nothing to draw off screen, the size above still has to be known for links and framing
    const bool visible = max.x >= m_origin.x && max.y >= m_origin.y && min.x <= m_origin.x + m_canvas_size.x && min.y <= m_origin.y + m_canvas_size.y;

    bool error          = false;
    const string notes  = DiagnosticFor(node.id, error);
    const bool selected = m_selection.count(node.id) != 0;
    const float rounding = 8.0f * zoom;

    if (visible)
    {
        const bool hovered_now = m_canvas_hovered && !m_link_drag.active && ImGui::IsMouseHoveringRect(min, max, false);
        const ImVec4 alert     = error ? ImGui::Style::color_error : ImGui::Style::color_warning;
        const ImVec4 hue       = to_vec4(NodeColor(definition));
        draw_glass_node(draw, min, max, header, rounding, zoom, hue, selected, hovered_now, notes.empty() ? nullptr : &alert);

        // events are where execution starts, a lit dot says so before the title is read
        float title_x = min.x + pad;
        if (is_event)
        {
            const float dot = 3.0f * zoom;
            ImGui::EditorUi::status_dot(draw, ImVec2(title_x + dot, min.y + header * 0.5f), dot, ImGui::Style::lerp(hue, ImVec4(1, 1, 1, 1), 0.35f));
            title_x += dot * 2.0f + pad * 0.6f;
        }
        draw->PushClipRect(min, ImVec2(max.x - pad * 0.5f, min.y + header), true);
        draw->AddText(Editor::font_bold, ImGui::GetFontSize(), ImVec2(title_x, min.y + (header - ImGui::GetFontSize()) * 0.5f), ImGui::EditorUi::color(ImVec4(1.0f, 1.0f, 1.0f, 0.96f)), title.c_str());
        draw->PopClipRect();
    }

    ImGui::PushID(static_cast<int>(node.id));

    ImGui::SetCursorScreenPos(min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##node", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    HandleNodeInteraction(node, ImGui::IsItemClicked(), ImGui::IsItemActive(), ImGui::IsItemActivated(), ImGui::IsItemDeactivated());
    if (hovered && !m_link_drag.active && !m_nodes_dragging && ImGui::GetMousePos().y < min.y + header)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(definition.id.c_str());
        if (!definition.tooltip.empty())
        {
            ImGui::TextUnformatted(definition.tooltip.c_str());
        }
        if (!notes.empty())
        {
            ImGui::TextColored(error ? ImGui::Style::color_error : ImGui::Style::color_warning, "%s", notes.c_str());
        }
        ImGui::EndTooltip();
    }

    auto pin_alpha = [&](const PinDefinition& pin, bool output)
    {
        if (!m_link_drag.active)
        {
            return 1.0f;
        }
        if (m_link_drag.from.node == node.id)
        {
            return m_link_drag.from.pin == pin.name && m_link_drag.from.output == output ? 1.0f : 0.3f;
        }
        if (m_link_drag.from.output == output || !pin.connectable)
        {
            return 0.3f;
        }
        const bool fits = output ? types_compatible(pin.type, m_link_drag.type) : types_compatible(m_link_drag.type, pin.type);
        return fits ? 1.0f : 0.3f;
    };

    auto pin_button = [&](const PinDefinition& pin, bool output, const ImVec2& center)
    {
        PinKey key;
        key.node   = node.id;
        key.pin    = pin.name;
        key.output = output;
        if (pin.connectable)
        {
            m_pin_positions[key] = center;
            m_pin_types[key]     = pin.type;
        }
        if (!pin.connectable)
        {
            return;
        }

        ImGui::PushID(output ? "out" : "in");
        ImGui::PushID(pin.name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(center.x - row * 0.5f, center.y - row * 0.5f));
        ImGui::InvisibleButton("##pin", ImVec2(row, row));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && io.KeyAlt)
        {
            BeginChange();
            m_graph.disconnect(node.id, pin.name, output);
            EndChange();
        }
        else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) && !m_link_drag.active && !io.KeyAlt)
        {
            // ctrl dragging off a fed data input picks its link up to move it elsewhere
            const Link* existing = !output && pin.type != type_exec ? m_graph.find_input_link(node.id, pin.name) : nullptr;
            if (existing && io.KeyCtrl)
            {
                m_link_drag.active      = true;
                m_link_drag.from.node   = existing->from_node;
                m_link_drag.from.pin    = existing->from_pin;
                m_link_drag.from.output = true;
                m_link_drag.type        = pin.type;
                BeginChange();
                m_graph.disconnect(node.id, pin.name, false);
            }
            else
            {
                m_link_drag.active = true;
                m_link_drag.from   = key;
                m_link_drag.type   = pin.type;
            }
        }
        if (ImGui::IsItemHovered() && !m_link_drag.active)
        {
            ImGui::SetTooltip("%s: %s%s", pin.name.c_str(), pin.type.c_str(), pin.optional ? " (optional)" : "");
        }
        ImGui::PopID();
        ImGui::PopID();
    };

    const float rows_top = min.y + header + pad * 0.3f;
    for (size_t i = 0; i < definition.inputs.size(); i++)
    {
        const PinDefinition& pin = definition.inputs[i];
        const ImVec2 center(min.x, rows_top + row * (static_cast<float>(i) + 0.5f));
        const bool connected = m_graph.is_connected(node.id, pin.name, false);
        if (visible && pin.connectable)
        {
            const bool pin_hot = m_pin_hovered && m_hovered_pin.node == node.id && !m_hovered_pin.output && m_hovered_pin.pin == pin.name;
            draw_pin_shape(draw, ImVec2(center.x + radius + pad * 0.4f, center.y), radius, pin.type, connected, pin_alpha(pin, false), pin_hot);
        }
        pin_button(pin, false, ImVec2(center.x + radius + pad * 0.4f, center.y));

        float x = min.x + radius * 2.0f + pad;
        if (visible && pin_label_visible(pin, false))
        {
            draw->AddText(ImVec2(x, center.y - ImGui::GetFontSize() * 0.5f), ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.82f)), pin.name.c_str());
            x += ImGui::CalcTextSize(pin.name.c_str()).x + pad * 0.5f;
        }
        if (visible && shows_value(pin))
        {
            if (widgets)
            {
                ImGui::SetCursorScreenPos(ImVec2(x, center.y - ImGui::GetFrameHeight() * 0.5f));
                DrawInlineValue(node, pin, inline_width(pin.type) * zoom);
            }
            else
            {
                auto value = node.values.find(pin.name);
                draw->AddText(ImVec2(x, center.y - ImGui::GetFontSize() * 0.5f), ImGui::EditorUi::color(ImGui::Style::color_text_muted), (value != node.values.end() ? value->second : pin.default_value).c_str());
            }
        }
    }

    for (size_t i = 0; i < definition.outputs.size(); i++)
    {
        const PinDefinition& pin = definition.outputs[i];
        const ImVec2 center(max.x - radius - pad * 0.4f, rows_top + row * (static_cast<float>(i) + 0.5f));
        const bool connected = m_graph.is_connected(node.id, pin.name, true);
        if (visible)
        {
            const bool pin_hot = m_pin_hovered && m_hovered_pin.node == node.id && m_hovered_pin.output && m_hovered_pin.pin == pin.name;
            draw_pin_shape(draw, center, radius, pin.type, connected, pin_alpha(pin, true), pin_hot);
            if (pin_label_visible(pin, true))
            {
                const float label = ImGui::CalcTextSize(pin.name.c_str()).x;
                draw->AddText(ImVec2(center.x - radius - pad * 0.6f - label, center.y - ImGui::GetFontSize() * 0.5f), ImGui::EditorUi::color(ImGui::Style::with_alpha(ImGui::Style::color_text, 0.82f)), pin.name.c_str());
            }
        }
        pin_button(pin, true, center);
    }

    ImGui::PopID();
}

void VisualScriptEditor::DrawLinks(ImDrawList* draw)
{
    const float zoom = m_graph.view_zoom;
    // a dark underlay keeps a wire readable over bright glass, a lit wire glows and carries a pulse in the direction it flows
    const float time = static_cast<float>(ImGui::GetTime());
    auto curve = [&](const ImVec2& a, const ImVec2& b, ImU32 col, float thickness, bool lit = false)
    {
        const float bend = std::max(fabsf(b.x - a.x) * 0.5f, 40.0f * zoom);
        const ImVec2 c1  = ImVec2(a.x + bend, a.y);
        const ImVec2 c2  = ImVec2(b.x - bend, b.y);
        draw->AddBezierCubic(a, c1, c2, b, IM_COL32(0, 0, 0, 90), (thickness + 2.5f) * zoom);
        if (lit)
        {
            draw->AddBezierCubic(a, c1, c2, b, with_alpha(col, 0.22f), (thickness + 5.0f) * zoom);
        }
        draw->AddBezierCubic(a, c1, c2, b, col, thickness * zoom);
        if (lit)
        {
            const float t     = fmodf(time * 0.8f, 1.0f);
            const ImVec2 dot  = ImBezierCubicCalc(a, c1, c2, b, t);
            draw->AddCircleFilled(dot, 4.5f * zoom, with_alpha(col, 0.35f));
            draw->AddCircleFilled(dot, 2.5f * zoom, IM_COL32_WHITE);
        }
    };

    for (const Link& link : m_graph.links)
    {
        PinKey from;
        from.node   = link.from_node;
        from.pin    = link.from_pin;
        from.output = true;
        PinKey to;
        to.node   = link.to_node;
        to.pin    = link.to_pin;
        to.output = false;
        auto a = m_pin_positions.find(from);
        auto b = m_pin_positions.find(to);
        if (a == m_pin_positions.end() || b == m_pin_positions.end())
        {
            continue;
        }
        const string& type = m_pin_types[from];
        const bool exec    = type == type_exec;
        const bool lit     = m_selection.count(link.from_node) || m_selection.count(link.to_node);
        curve(a->second, b->second, with_alpha(pin_color(type), lit ? 1.0f : 0.75f), exec ? 3.0f : 2.0f, lit);
    }

    if (m_link_drag.active)
    {
        auto start = m_pin_positions.find(m_link_drag.from);
        if (start != m_pin_positions.end())
        {
            ImVec2 a = start->second;
            ImVec2 b = ImGui::GetIO().MousePos;
            if (!m_link_drag.from.output)
            {
                swap(a, b);
            }
            curve(a, b, pin_color(m_link_drag.type), 2.5f);
        }
    }
}

void VisualScriptEditor::FinishLinkDrag()
{
    LinkDrag drag      = m_link_drag;
    m_link_drag.active = false;

    if (m_pin_hovered && !(m_hovered_pin.node == drag.from.node && m_hovered_pin.pin == drag.from.pin && m_hovered_pin.output == drag.from.output))
    {
        if (m_hovered_pin.output != drag.from.output)
        {
            const PinKey& out = drag.from.output ? drag.from : m_hovered_pin;
            const PinKey& in  = drag.from.output ? m_hovered_pin : drag.from;
            string error;
            BeginChange();
            if (!connect(m_graph, out.node, out.pin, in.node, in.pin, &error))
            {
                m_status = error;
            }
            EndChange();
            return;
        }
        EndChange();
        return;
    }

    // dropped on empty space, offer the nodes that can take it
    if (m_canvas_hovered)
    {
        m_add_menu_request = true;
        m_add_menu_canvas  = ToCanvas(ImGui::GetIO().MousePos);
        m_add_menu_link    = drag;
        m_add_menu_link.active = true;
    }
    EndChange();
}

void VisualScriptEditor::HandleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }

    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        Save();
    }
    else if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
    {
        Redo();
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        Undo();
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        DuplicateSelection();
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
    {
        CopySelection();
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
    {
        CopySelection();
        DeleteSelection();
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
    {
        const ImVec2 at = ToCanvas(io.MousePos);
        Paste(at.x, at.y);
    }
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
    {
        m_selection.clear();
        for (const Node& node : m_graph.nodes)
        {
            m_selection.insert(node.id);
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
    {
        DeleteSelection();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        FrameAll();
    }
    else if ((ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false)) && m_canvas_hovered)
    {
        m_add_menu_request = true;
        m_add_menu_canvas  = ToCanvas(io.MousePos);
        m_add_menu_link    = LinkDrag();
    }
}

void VisualScriptEditor::DeleteSelection()
{
    if (m_selection.empty())
    {
        return;
    }
    BeginChange();
    for (uint32_t id : m_selection)
    {
        m_graph.remove_node(id);
    }
    m_selection.clear();
    EndChange();
}

void VisualScriptEditor::CopySelection()
{
    if (!m_selection.empty())
    {
        m_clipboard = copy_nodes(m_graph, m_selection);
        ImGui::SetClipboardText(m_clipboard.c_str());
    }
}

void VisualScriptEditor::Paste(float x, float y)
{
    // the system clipboard first, so graphs travel between editor instances and through chat
    const char* system = ImGui::GetClipboardText();
    const string xml   = system && strstr(system, "<visual_script") ? string(system) : m_clipboard;
    if (xml.empty())
    {
        return;
    }
    BeginChange();
    const vector<uint32_t> pasted = paste_nodes(m_graph, xml, x, y);
    m_selection = set<uint32_t>(pasted.begin(), pasted.end());
    EndChange();
}

void VisualScriptEditor::DuplicateSelection()
{
    if (m_selection.empty())
    {
        return;
    }
    float x = FLT_MAX, y = FLT_MAX;
    for (const Node& node : m_graph.nodes)
    {
        if (m_selection.count(node.id))
        {
            x = min(x, node.x);
            y = min(y, node.y);
        }
    }
    const string xml = copy_nodes(m_graph, m_selection);
    BeginChange();
    const vector<uint32_t> pasted = paste_nodes(m_graph, xml, x + 40.0f, y + 40.0f);
    m_selection = set<uint32_t>(pasted.begin(), pasted.end());
    EndChange();
}

// menus

void VisualScriptEditor::DrawAddMenu()
{
    if (m_add_menu_request)
    {
        m_add_menu_request = false;
        m_search[0]        = 0;
        m_search_selected  = 0;
        m_search_focus     = true;

        // what the menu offers: everything, minus events the graph already has, minus what can't take a dragged link
        m_menu_definitions.clear();
        set<string> present;
        for (const Node& node : m_graph.nodes)
        {
            present.insert(node.type);
        }
        auto consider = [&](const NodeDefinition& definition)
        {
            if (is_lifecycle_event(definition) && present.count(definition.id))
            {
                return;
            }
            if (m_add_menu_link.active)
            {
                if (definition.kind == NodeKind::Comment)
                {
                    return;
                }
                const vector<PinDefinition>& pins = m_add_menu_link.from.output ? definition.inputs : definition.outputs;
                const bool fits = any_of(pins.begin(), pins.end(), [&](const PinDefinition& pin)
                {
                    return pin.connectable && (m_add_menu_link.from.output ? types_compatible(m_add_menu_link.type, pin.type) : types_compatible(pin.type, m_add_menu_link.type));
                });
                if (!fits)
                {
                    return;
                }
            }
            m_menu_definitions.push_back(definition);
        };
        for (const NodeDefinition& definition : get_definitions())
        {
            consider(definition);
        }
        for (const NodeDefinition& definition : get_graph_definitions(m_graph))
        {
            consider(definition);
        }
        stable_sort(m_menu_definitions.begin(), m_menu_definitions.end(), [](const NodeDefinition& a, const NodeDefinition& b)
        {
            return a.category != b.category ? a.category < b.category : a.title < b.title;
        });
        ImGui::OpenPopup("##add_node");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 120.0f), ImVec2(380.0f, 520.0f));
    if (!ImGui::BeginPopup("##add_node"))
    {
        m_add_menu_link = LinkDrag();
        return;
    }

    if (m_search_focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_search_focus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::EditorUi::scaled(4.0f));
    if (ImGui::InputTextWithHint("##search", m_add_menu_link.active ? "Search nodes that fit the link..." : "Search nodes...", m_search, sizeof(m_search)))
    {
        m_search_selected = 0;
    }
    ImGui::EditorUi::decorate_field();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

    const NodeDefinition* picked = nullptr;
    const string query           = lower(m_search);

    if (!query.empty())
    {
        // every word has to appear somewhere, titles that start with the query come first
        vector<string> words;
        size_t start = 0;
        while (start < query.size())
        {
            size_t end = query.find(' ', start);
            end        = end == string::npos ? query.size() : end;
            if (end > start)
            {
                words.push_back(query.substr(start, end - start));
            }
            start = end + 1;
        }

        vector<pair<int, const NodeDefinition*>> results;
        for (const NodeDefinition& definition : m_menu_definitions)
        {
            const string title    = lower(definition.title);
            const string haystack = title + " " + lower(definition.category) + " " + lower(definition.id) + " " + lower(definition.tooltip);
            bool all = true;
            for (const string& word : words)
            {
                all = all && haystack.find(word) != string::npos;
            }
            if (!all)
            {
                continue;
            }
            const int score = title.rfind(query, 0) == 0 ? 0 : (title.find(query) != string::npos ? 1 : (lower(definition.id).find(query) != string::npos ? 2 : 3));
            results.push_back({ score, &definition });
        }
        stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b)
        {
            return a.first != b.first ? a.first < b.first : a.second->title.size() < b.second->title.size();
        });
        if (results.size() > k_max_results)
        {
            results.resize(k_max_results);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            m_search_selected = min(m_search_selected + 1, static_cast<int>(results.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            m_search_selected = std::max(m_search_selected - 1, 0);
        }
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

        ImGui::BeginChild("##results", ImVec2(0.0f, 420.0f));
        for (size_t i = 0; i < results.size(); i++)
        {
            const NodeDefinition& definition = *results[i].second;
            ImGui::PushID(static_cast<int>(i));
            const bool selected = static_cast<int>(i) == m_search_selected;
            if (ImGui::Selectable(definition.title.c_str(), selected) || (selected && enter))
            {
                picked = &definition;
            }
            if (selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            {
                ImGui::SetScrollHereY();
            }
            if (ImGui::IsItemHovered() && !definition.tooltip.empty())
            {
                ImGui::SetTooltip("%s\n%s", definition.id.c_str(), definition.tooltip.c_str());
            }
            const float category_w = ImGui::CalcTextSize(definition.category.c_str()).x;
            const float content_right = ImGui::GetCurrentWindow()->ContentRegionRect.Max.x - ImGui::GetWindowPos().x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), content_right - category_w));
            ImGui::TextColored(ImGui::Style::color_text_faint, "%s", definition.category.c_str());
            ImGui::PopID();
        }
        if (results.empty())
        {
            ImGui::TextColored(ImGui::Style::color_text_muted, "Nothing matches.");
        }
        ImGui::EndChild();
    }
    else
    {
        // browse by category, "Math|Number" nests one level
        ImGui::BeginChild("##browse", ImVec2(0.0f, 420.0f));
        size_t i = 0;
        while (i < m_menu_definitions.size())
        {
            const string& category = m_menu_definitions[i].category;
            const size_t bar       = category.find('|');
            const string top       = category.substr(0, bar);
            size_t end             = i;
            while (end < m_menu_definitions.size() && m_menu_definitions[end].category.substr(0, m_menu_definitions[end].category.find('|')) == top)
            {
                end++;
            }

            if (ImGui::TreeNodeEx(top.c_str(), m_add_menu_link.active ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            {
                size_t j = i;
                while (j < end)
                {
                    const string& sub_category = m_menu_definitions[j].category;
                    const size_t sub_bar       = sub_category.find('|');
                    size_t sub_end             = j;
                    while (sub_end < end && m_menu_definitions[sub_end].category == sub_category)
                    {
                        sub_end++;
                    }
                    const bool nested = sub_bar != string::npos;
                    const bool open   = !nested || ImGui::TreeNode(sub_category.substr(sub_bar + 1).c_str());
                    if (open)
                    {
                        for (size_t k = j; k < sub_end; k++)
                        {
                            ImGui::PushID(static_cast<int>(k));
                            if (ImGui::Selectable(m_menu_definitions[k].title.c_str()))
                            {
                                picked = &m_menu_definitions[k];
                            }
                            if (ImGui::IsItemHovered() && !m_menu_definitions[k].tooltip.empty())
                            {
                                ImGui::SetTooltip("%s\n%s", m_menu_definitions[k].id.c_str(), m_menu_definitions[k].tooltip.c_str());
                            }
                            ImGui::PopID();
                        }
                        if (nested)
                        {
                            ImGui::TreePop();
                        }
                    }
                    j = sub_end;
                }
                ImGui::TreePop();
            }
            i = end;
        }
        ImGui::EndChild();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }

    if (picked)
    {
        BeginChange();
        string error;
        if (Node* node = add_node(m_graph, picked->id, m_add_menu_canvas.x, m_add_menu_canvas.y, &error))
        {
            const uint32_t id = node->id;
            m_selection       = { id };
            if (m_add_menu_link.active)
            {
                const string pin = find_compatible_pin(m_graph, id, m_add_menu_link.type, !m_add_menu_link.from.output);
                if (!pin.empty())
                {
                    if (m_add_menu_link.from.output)
                    {
                        connect(m_graph, m_add_menu_link.from.node, m_add_menu_link.from.pin, id, pin, &error);
                    }
                    else
                    {
                        connect(m_graph, id, pin, m_add_menu_link.from.node, m_add_menu_link.from.pin, &error);
                    }
                }
                // a node dropped to the left of the input it feeds reads better
                if (!m_add_menu_link.from.output)
                {
                    if (Node* added = m_graph.find_node(id))
                    {
                        added->x -= 220.0f;
                    }
                }
            }
        }
        else
        {
            m_status = error;
        }
        EndChange();
        m_add_menu_link = LinkDrag();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void VisualScriptEditor::DrawOpenPopup()
{
    if (m_open_request)
    {
        m_open_request = false;

        // visual scripts are lua files with a graph block, look where scripts usually live
        m_open_candidates.clear();
        vector<string> roots = { filesystem::current_path().string() };
        if (!World::GetFilePath().empty())
        {
            roots.push_back(FileSystem::GetDirectoryFromFilePath(World::GetFilePath()));
        }
        set<string> seen;
        for (const string& root : roots)
        {
            error_code code;
            for (filesystem::recursive_directory_iterator it(root, filesystem::directory_options::skip_permission_denied, code), end; it != end && !code; it.increment(code))
            {
                if (it->is_regular_file(code) && it->path().extension() == ".lua")
                {
                    const string path = filesystem::weakly_canonical(it->path(), code).generic_string();
                    if (seen.insert(path).second && is_visual_script_file(path))
                    {
                        m_open_candidates.push_back(path);
                    }
                }
            }
        }
        sort(m_open_candidates.begin(), m_open_candidates.end());

        const string resources = World::GetResourceDirectory();
        m_new_directory        = IsOpen() ? FileSystem::GetDirectoryFromFilePath(m_path) : (resources.empty() ? string("project/scripts") : resources);
        ImGui::OpenPopup("##visual_script_open");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 0.0f), ImVec2(900.0f, 600.0f));
    if (!ImGui::BeginPopup("##visual_script_open"))
    {
        return;
    }

    ImGui::EditorUi::section_rule("New visual script");
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Folder", &m_new_directory);
    ImGui::EditorUi::decorate_field();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Name", m_new_name, sizeof(m_new_name));
    ImGui::EditorUi::decorate_field();
    ImGui::SameLine();
    if (editor_ui::primary_button("Create"))
    {
        string directory = m_new_directory;
        if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
        {
            directory += "/";
        }
        string path = directory + m_new_name;
        if (FileSystem::GetExtensionFromFilePath(path) != ".lua")
        {
            path += ".lua";
        }
        FileSystem::CreateDirectory_(directory);
        string error;
        if (!Create(path, &error))
        {
            m_status = error;
        }
        ImGui::CloseCurrentPopup();
    }

    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(6.0f)));
    ImGui::EditorUi::section_rule("Open");
    if (m_open_candidates.empty())
    {
        editor_ui::layout::caption("No visual scripts found under the working directory or next to the world.");
    }
    ImGui::BeginChild("##candidates", ImVec2(0.0f, min(400.0f, 24.0f * static_cast<float>(m_open_candidates.size()) + 8.0f)));
    for (const string& path : m_open_candidates)
    {
        if (ImGui::Selectable(path.c_str(), path == m_path))
        {
            string error;
            if (!Open(path, &error))
            {
                m_status = error;
            }
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void VisualScriptEditor::DrawLuaWindow()
{
    if (!m_show_lua)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(700.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Generated Lua", &m_show_lua))
    {
        // the graph block is for the editor, the code below it is what runs
        const string& source = m_compiled.source;
        size_t start         = source.find("\nlocal script");
        start                = start == string::npos ? 0 : start + 1;
        editor_ui::layout::caption("Read only, regenerated on every change. Lines map back to nodes through their -- comments.");
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
        begin_pane("##lua", ImVec2(0.0f, 0.0f), ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(Editor::font_mono ? Editor::font_mono : ImGui::GetFont(), 0.0f);
        ImGui::TextUnformatted(source.c_str() + start, source.c_str() + source.size());
        ImGui::PopFont();
        ImGui::EndChild();
    }
    ImGui::End();
}
