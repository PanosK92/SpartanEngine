/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================================
#include "Widget.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include "scripting/visual/VisualCompiler.h"
#include "scripting/visual/VisualGraph.h"
#include "scripting/visual/VisualNodes.h"
//==============================================

namespace spartan
{
    class Entity;
}

// a node graph editor for visual scripts, lua files that carry their graph and the code generated from it
class VisualScriptEditor : public Widget
{
public:
    VisualScriptEditor(Editor* editor);

    void OnTickVisible() override;

    // the open document, the mcp commands drive the same one the user sees
    bool Open(const std::string& path, std::string* error = nullptr);
    bool Create(const std::string& path, std::string* error = nullptr);
    spartan::scripting::visual::CompileResult Save(uint32_t* reloaded_components = nullptr);
    const spartan::scripting::visual::CompileResult& Compile();
    bool IsOpen() const                                       { return !m_path.empty(); }
    bool IsDirty() const                                      { return m_dirty; }
    const std::string& GetPath() const                        { return m_path; }
    spartan::scripting::visual::Graph& GetGraph()             { return m_graph; }
    const spartan::scripting::visual::CompileResult& GetCompiled() const { return m_compiled; }

    // wrap edits made from outside the canvas so they can be undone, end commits only when the graph changed
    void BeginChange();
    void EndChange();
    void Undo();
    void Redo();

    void FocusNode(uint32_t id);
    void FrameAll();
    const std::set<uint32_t>& GetSelection() const { return m_selection; }
    void SetSelection(const std::set<uint32_t>& selection) { m_selection = selection; }

    // a free spot to the right of everything, for nodes added without a position
    void GetFreePosition(float& x, float& y) const;
    // attaches the open script to an entity, adding a script component when it has none
    bool AttachToEntity(spartan::Entity* entity, std::string* error = nullptr);
    bool AttachToSelectedEntity(std::string* error = nullptr);

private:
    struct PinKey
    {
        uint32_t node = 0;
        std::string pin;
        bool output   = false;
        bool operator<(const PinKey& other) const;
    };

    struct LinkDrag
    {
        bool active = false;
        PinKey from;
        std::string type;
    };

    void DrawToolbar();
    void DrawSidePanel();
    void DrawVariables();
    void DrawDiagnostics();
    void DrawCanvas();
    void DrawGrid(ImDrawList* draw, const ImVec2& min, const ImVec2& max);
    void DrawNode(ImDrawList* draw, spartan::scripting::visual::Node& node, const spartan::scripting::visual::NodeDefinition& definition);
    void DrawComment(ImDrawList* draw, spartan::scripting::visual::Node& node);
    bool DrawInlineValue(spartan::scripting::visual::Node& node, const spartan::scripting::visual::PinDefinition& pin, float width);
    void DrawLinks(ImDrawList* draw);
    void DrawAddMenu();
    void DrawOpenPopup();
    void DrawLuaWindow();
    void HandleShortcuts();
    void HandleNodeInteraction(spartan::scripting::visual::Node& node, bool clicked, bool active, bool activated, bool deactivated);
    void FinishLinkDrag();

    void DeleteSelection();
    void DuplicateSelection();
    void CopySelection();
    void Paste(float x, float y);
    void MarkChanged();

    ImVec2 ToScreen(float x, float y) const;
    ImVec2 ToCanvas(const ImVec2& screen) const;
    uint32_t NodeColor(const spartan::scripting::visual::NodeDefinition& definition) const;
    std::string DiagnosticFor(uint32_t node, bool& error) const;

    // document
    std::string m_path;
    spartan::scripting::visual::Graph m_graph;
    spartan::scripting::visual::CompileResult m_compiled;
    bool m_dirty           = false;
    bool m_compile_pending = false;
    std::string m_status;
    std::vector<std::string> m_undo;
    std::vector<std::string> m_redo;
    std::string m_change_before;
    bool m_change_open = false;

    // view
    ImVec2 m_origin       = ImVec2(0.0f, 0.0f);
    ImVec2 m_canvas_size  = ImVec2(0.0f, 0.0f);
    bool m_frame_pending  = false;
    uint32_t m_focus_node = 0;
    bool m_show_lua       = false;
    bool m_show_side      = true;

    // interaction
    std::set<uint32_t> m_selection;
    std::map<PinKey, ImVec2> m_pin_positions;
    std::map<PinKey, std::string> m_pin_types;
    PinKey m_menu_pin;
    uint32_t m_bring_to_front = 0;
    std::map<uint32_t, ImVec2> m_node_sizes; // canvas units, from the last frame
    PinKey m_hovered_pin;
    bool m_pin_hovered       = false;
    LinkDrag m_link_drag;
    bool m_box_selecting     = false;
    ImVec2 m_box_start       = ImVec2(0.0f, 0.0f);
    bool m_nodes_dragging    = false;
    bool m_canvas_hovered    = false;
    std::string m_clipboard;

    // add node menu
    bool m_add_menu_request  = false;
    ImVec2 m_add_menu_canvas = ImVec2(0.0f, 0.0f);
    LinkDrag m_add_menu_link; // a link dropped on empty space, the new node gets wired to it
    std::vector<spartan::scripting::visual::NodeDefinition> m_menu_definitions;
    char m_search[128]       = {};
    int m_search_selected    = 0;
    bool m_search_focus      = false;

    // open popup
    bool m_open_request = false;
    std::vector<std::string> m_open_candidates;
    std::string m_new_directory;
    char m_new_name[128] = "new_visual_script";
};
