/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "Editor.h"
#include "widgets/AssetViewer.h"
#include "file_system/FileSystem.h"
#include "rendering/Renderer.h"
#include "world/World.h"
#include <limits>
//=====================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace editor_mcp
{
    namespace
    {
        // a capture always lands in the library's thumbnail directory, a caller only names the file
        string capture_path(const string* requested)
        {
            string name = requested
                ? FileSystem::GetFileNameFromFilePath(*requested)
                : "asset_viewer.png";
            if (FileSystem::GetExtensionFromFilePath(name) != ".png")
            {
                name += ".png";
            }
            return
                World::GetLibraryResourceDirectory() +
                "thumbnails/" +
                name;
        }

        optional<AssetViewer::PreviewView> as_view(const string& value)
        {
            const string normalized = to_lower(value);
            if (normalized == "front")
            {
                return AssetViewer::PreviewView::Front;
            }
            if (normalized == "back")
            {
                return AssetViewer::PreviewView::Back;
            }
            if (normalized == "left")
            {
                return AssetViewer::PreviewView::Left;
            }
            if (normalized == "right")
            {
                return AssetViewer::PreviewView::Right;
            }
            if (normalized == "top")
            {
                return AssetViewer::PreviewView::Top;
            }
            if (normalized == "bottom")
            {
                return AssetViewer::PreviewView::Bottom;
            }
            if (normalized == "perspective")
            {
                return AssetViewer::PreviewView::Perspective;
            }
            return nullopt;
        }

        optional<AssetViewer::PreviewShading> as_shading(const string& value)
        {
            const string normalized = to_lower(value);
            if (normalized == "solid")
            {
                return AssetViewer::PreviewShading::Solid;
            }
            if (
                normalized == "wire" ||
                normalized == "wireframe"
            )
            {
                return AssetViewer::PreviewShading::Wireframe;
            }
            if (
                normalized == "vertices" ||
                normalized == "points"
            )
            {
                return AssetViewer::PreviewShading::Vertices;
            }
            return nullopt;
        }

        optional<AssetViewer::PreviewBackdrop> as_backdrop(const string& value)
        {
            const string normalized = to_lower(value);
            if (normalized == "auto")
            {
                return AssetViewer::PreviewBackdrop::Auto;
            }
            if (normalized == "sky")
            {
                return AssetViewer::PreviewBackdrop::Sky;
            }
            if (normalized == "charcoal")
            {
                return AssetViewer::PreviewBackdrop::Charcoal;
            }
            if (normalized == "slate")
            {
                return AssetViewer::PreviewBackdrop::Slate;
            }
            if (normalized == "paper")
            {
                return AssetViewer::PreviewBackdrop::Paper;
            }
            return nullopt;
        }

        const char* shading_name(AssetViewer::PreviewShading shading)
        {
            switch (shading)
            {
                case AssetViewer::PreviewShading::Wireframe:
                    return "wire";
                case AssetViewer::PreviewShading::Vertices:
                    return "vertices";
                default:
                    return "solid";
            }
        }

        const char* backdrop_name(AssetViewer::PreviewBackdrop backdrop)
        {
            switch (backdrop)
            {
                case AssetViewer::PreviewBackdrop::Auto:
                    return "auto";
                case AssetViewer::PreviewBackdrop::Sky:
                    return "sky";
                case AssetViewer::PreviewBackdrop::Charcoal:
                    return "charcoal";
                case AssetViewer::PreviewBackdrop::Paper:
                    return "paper";
                default:
                    return "slate";
            }
        }

        string string_array(const vector<string>& values)
        {
            string json = "[";
            for (size_t index = 0; index < values.size(); index++)
            {
                if (index > 0)
                {
                    json += ",";
                }
                json += quote(values[index]);
            }
            return json + "]";
        }

        vector<string> string_list(const string* value)
        {
            vector<string> values;
            if (!value)
            {
                return values;
            }

            string current;
            bool quoted = false;
            bool escaped = false;
            const auto append =
                [&values](string item)
            {
                const size_t first =
                    item.find_first_not_of(" \t\r\n");
                const size_t last =
                    item.find_last_not_of(" \t\r\n");
                if (first != string::npos)
                {
                    values.push_back(
                        item.substr(first, last - first + 1)
                    );
                }
            };
            for (const char character : *value)
            {
                if (escaped)
                {
                    current += character;
                    escaped = false;
                    continue;
                }
                if (quoted && character == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (character == '"')
                {
                    quoted = !quoted;
                    continue;
                }
                if (
                    !quoted &&
                    (
                        character == ',' ||
                        character == '[' ||
                        character == ']'
                    )
                )
                {
                    append(current);
                    current.clear();
                    continue;
                }
                current += character;
            }
            append(current);
            return values;
        }

        optional<bool> strict_bool(const string* value)
        {
            if (!value)
            {
                return nullopt;
            }
            const string normalized = to_lower(*value);
            if (
                normalized == "true" ||
                normalized == "1" ||
                normalized == "yes" ||
                normalized == "on"
            )
            {
                return true;
            }
            if (
                normalized == "false" ||
                normalized == "0" ||
                normalized == "no" ||
                normalized == "off"
            )
            {
                return false;
            }
            return nullopt;
        }

        bool read_bool_value(
            const string* value,
            const char* name,
            optional<bool>& result,
            string& error
        )
        {
            if (!value)
            {
                return true;
            }
            result = strict_bool(value);
            if (!result)
            {
                error = string(name) + " must be true or false";
                return false;
            }
            return true;
        }

        bool read_bool(
            const McpRequest& request,
            const char* name,
            optional<bool>& result,
            string& error
        )
        {
            return read_bool_value(
                find(request, name),
                name,
                result,
                error
            );
        }

        string asset_summary_json(
            const AssetViewer::AssetSummary& asset
        )
        {
            return
                "{\"id\":" + quote(asset.id) +
                ",\"name\":" + quote(asset.name) +
                ",\"type\":" + quote(asset.type) +
                ",\"path\":" + quote(asset.path) +
                ",\"source_path\":" + quote(asset.source_path) +
                ",\"thumbnail_path\":" +
                quote(asset.thumbnail_path) +
                ",\"quality_score\":" +
                to_string(asset.quality_score) +
                ",\"quality_verified\":" +
                boolean(asset.quality_verified) +
                ",\"disk_only\":" +
                boolean(asset.disk_only) +
                "}";
        }

        // every command that changes the panel answers with the same picture of it, so a caller never
        // has to follow a change with a status call
        string status_reply(const AssetViewer* viewer)
        {
            const AssetViewer::PreviewStatus status =
                viewer->GetPreviewStatus();
            return
                "{\"ok\":true,\"visible\":" +
                string(boolean(status.visible)) +
                ",\"selected_asset_id\":" +
                quote(status.selected_asset_id) +
                ",\"selected_asset_name\":" +
                quote(status.selected_asset_name) +
                ",\"loaded_path\":" +
                quote(status.loaded_path) +
                ",\"status_message\":" +
                quote(status.status_message) +
                ",\"catalog_path\":" +
                quote(status.catalog_path) +
                ",\"catalog_count\":" +
                to_string(status.catalog_count) +
                ",\"selected_asset_ids\":" +
                string_array(status.selected_asset_ids) +
                ",\"dependency_path\":" +
                quote(status.dependency_path) +
                ",\"preview_entity_id\":" +
                quote(
                    status.previewed_entity_id != 0
                        ? to_string(status.previewed_entity_id)
                        : ""
                ) +
                ",\"yaw\":" +
                to_string(status.yaw) +
                ",\"pitch\":" +
                to_string(status.pitch) +
                ",\"zoom\":" +
                to_string(status.zoom) +
                ",\"shading\":" +
                quote(shading_name(status.shading)) +
                ",\"backdrop\":" +
                quote(backdrop_name(status.backdrop)) +
                ",\"show_stats\":" +
                boolean(status.show_stats) +
                ",\"auto_rotate\":" +
                boolean(status.auto_rotate) +
                ",\"preview_lod\":" +
                to_string(status.preview_lod) +
                ",\"renderer_ready\":" +
                string(boolean(Renderer::IsSecondaryViewReady())) +
                ",\"renderer_generation\":" +
                to_string(Renderer::GetSecondaryViewGeneration()) +
                ",\"vertex_count\":" +
                to_string(status.vertex_count) +
                ",\"index_count\":" +
                to_string(status.index_count) +
                ",\"mesh_editable\":" +
                boolean(status.mesh_editable) +
                ",\"mesh_modified\":" +
                boolean(status.mesh_modified) +
                ",\"mesh_lods_built\":" +
                boolean(status.mesh_lods_built) +
                ",\"mesh_lods_attempted\":" +
                boolean(status.mesh_lods_attempted) +
                ",\"mesh_source_vertex_count\":" +
                to_string(status.mesh_source_vertices) +
                ",\"mesh_source_index_count\":" +
                to_string(status.mesh_source_indices) +
                ",\"mesh_working_vertex_count\":" +
                to_string(status.mesh_working_vertices) +
                ",\"mesh_working_index_count\":" +
                to_string(status.mesh_working_indices) +
                ",\"mesh_target_ratio\":" +
                to_string(status.mesh_target_ratio) +
                ",\"mesh_generate_lods\":" +
                boolean(status.mesh_generate_lods) +
                ",\"lod_count\":" +
                to_string(status.lod_count) +
                ",\"has_preview_content\":" +
                boolean(status.has_preview_content) +
                "}";
        }

        string revision_status_reply(
            const AssetViewer::RevisionStatus& status
        )
        {
            return
                "{\"ok\":true,\"candidate_active\":" +
                string(boolean(status.candidate_active)) +
                ",\"base_asset_id\":" +
                quote(status.base_asset_id) +
                ",\"candidate_path\":" +
                quote(status.candidate_path) +
                ",\"manifest_path\":" +
                quote(status.manifest_path) +
                ",\"generation\":" +
                to_string(status.generation) +
                ",\"base_entity_count\":" +
                to_string(status.base_entity_count) +
                ",\"candidate_entity_count\":" +
                to_string(status.candidate_entity_count) +
                ",\"base_dependency_count\":" +
                to_string(status.base_dependency_count) +
                ",\"candidate_dependency_count\":" +
                to_string(status.candidate_dependency_count) +
                ",\"candidate_previewed\":" +
                boolean(status.candidate_previewed) +
                ",\"request_pending\":" +
                boolean(status.request_pending) +
                ",\"request_action\":" +
                quote(status.request_action) +
                ",\"request_error\":" +
                quote(status.request_error) +
                ",\"request_path\":" +
                quote(status.request_path) +
                "}";
        }
    }

    void register_asset_viewer(Editor* editor)
    {
        add(
            "asset_viewer_open",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                viewer->SetPanelVisible(
                    as_bool(find(request, "visible")).value_or(true)
                );
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_status",
            [editor](const McpRequest&) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_revision_status",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                const string* asset_id = find(request, "asset_id");
                return revision_status_reply(
                    viewer->GetRevisionStatus(
                        asset_id ? *asset_id : ""
                    )
                );
            }
        );

        add(
            "asset_viewer_revision_preview",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                uint64_t generation = 0;
                if (const string* value = find(request, "generation"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (!parsed || *parsed == 0)
                    {
                        return failure(
                            "generation must be a positive integer"
                        );
                    }
                    generation = *parsed;
                }

                const string* asset_id = find(request, "asset_id");
                string error;
                if (
                    !viewer->PreviewRevision(
                        asset_id ? *asset_id : "",
                        generation,
                        error
                    )
                )
                {
                    return failure(error);
                }
                return revision_status_reply(
                    viewer->GetRevisionStatus()
                );
            }
        );

        const auto register_revision_request =
            [editor](
                const char* command,
                const bool apply
            )
        {
            add(
                command,
                [editor, apply](const McpRequest& request) -> string
                {
                    AssetViewer* viewer =
                        editor->GetWidget<AssetViewer>();
                    if (!viewer)
                    {
                        return failure(
                            "the asset viewer is not available"
                        );
                    }

                    const optional<bool> confirm =
                        strict_bool(find(request, "confirm"));
                    if (!confirm || !*confirm)
                    {
                        return failure(
                            apply
                                ? "confirm=true is required to apply an asset revision"
                                : "confirm=true is required to discard an asset revision"
                        );
                    }
                    const optional<uint64_t> generation = as_uint(
                        find(request, "generation")
                    );
                    if (!generation || *generation == 0)
                    {
                        return failure(
                            "generation from asset_viewer_revision_status is required"
                        );
                    }

                    const string* asset_id =
                        find(request, "asset_id");
                    string error;
                    const bool requested = apply
                        ? viewer->RequestRevisionApply(
                            asset_id ? *asset_id : "",
                            *generation,
                            true,
                            error
                        )
                        : viewer->RequestRevisionDiscard(
                            asset_id ? *asset_id : "",
                            *generation,
                            true,
                            error
                        );
                    if (!requested)
                    {
                        return failure(error);
                    }
                    return revision_status_reply(
                        viewer->GetRevisionStatus()
                    );
                }
            );
        };
        register_revision_request(
            "asset_viewer_revision_apply",
            true
        );
        register_revision_request(
            "asset_viewer_revision_discard",
            false
        );

        add(
            "asset_viewer_refresh",
            [editor](const McpRequest&) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                string error;
                if (!viewer->Refresh(error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_list",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                AssetViewer::ListRequest list;
                if (const string* value = find(request, "query"))
                {
                    list.query = *value;
                }
                if (const string* value = find(request, "type"))
                {
                    list.type = *value;
                }
                if (const string* value = find(request, "sort"))
                {
                    list.sort = *value;
                }
                if (const string* value = find(request, "offset"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (!parsed)
                    {
                        return failure(
                            "offset must be a non-negative integer"
                        );
                    }
                    list.offset = *parsed;
                }
                if (const string* value = find(request, "limit"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (!parsed)
                    {
                        return failure(
                            "limit must be a non-negative integer"
                        );
                    }
                    list.limit = *parsed;
                }
                optional<bool> include_disk_only;
                string error;
                if (
                    !read_bool(
                        request,
                        "include_disk_only",
                        include_disk_only,
                        error
                    )
                )
                {
                    return failure(error);
                }
                if (include_disk_only)
                {
                    list.include_disk_only = *include_disk_only;
                }

                AssetViewer::ListResult result;
                if (!viewer->ListAssets(list, result, error))
                {
                    return failure(error);
                }

                string json =
                    "{\"ok\":true,\"total\":" +
                    to_string(result.total) +
                    ",\"offset\":" +
                    to_string(result.offset) +
                    ",\"limit\":" +
                    to_string(result.limit) +
                    ",\"assets\":[";
                for (
                    size_t index = 0;
                    index < result.assets.size();
                    index++
                )
                {
                    if (index > 0)
                    {
                        json += ",";
                    }
                    json += asset_summary_json(result.assets[index]);
                }
                return json + "]}";
            }
        );

        add(
            "asset_viewer_inspect",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const string* asset_id = find(request, "asset_id");
                if (!asset_id || asset_id->empty())
                {
                    return failure("asset_id is required");
                }
                AssetViewer::AssetInspection result;
                string error;
                if (
                    !viewer->InspectAsset(
                        *asset_id,
                        result,
                        error
                    )
                )
                {
                    return failure(error);
                }

                string json =
                    "{\"ok\":true,\"asset\":" +
                    asset_summary_json(result.asset) +
                    ",\"aliases\":" +
                    string_array(result.aliases) +
                    ",\"tags\":" +
                    string_array(result.tags) +
                    ",\"dependencies\":" +
                    string_array(result.dependencies) +
                    ",\"missing_dependencies\":" +
                    string_array(result.missing_dependencies);
                json +=
                    ",\"technical\":{\"vertex_count\":" +
                    to_string(result.vertex_count) +
                    ",\"index_count\":" +
                    to_string(result.index_count) +
                    ",\"triangle_count\":" +
                    to_string(result.index_count / 3) +
                    ",\"texture_width\":" +
                    to_string(result.texture_width) +
                    ",\"texture_height\":" +
                    to_string(result.texture_height) +
                    ",\"texture_channels\":" +
                    to_string(result.texture_channels) +
                    ",\"source_exists\":" +
                    boolean(result.source_exists) +
                    ",\"source_bytes\":" +
                    to_string(result.source_bytes) +
                    ",\"prefab_entity_count\":" +
                    to_string(result.prefab_entity_count) +
                    "}}";
                return json;
            }
        );

        add(
            "asset_viewer_set_selection",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                AssetViewer::SelectionRequest selection;
                selection.asset_ids = string_list(
                    find_any(
                        request,
                        { "asset_ids", "ids", "asset_id" }
                    )
                );
                const string* mode = find(request, "mode");
                const string normalized =
                    mode ? to_lower(*mode) : "replace";
                if (normalized == "replace")
                {
                    selection.mode =
                        AssetViewer::SelectionMode::Replace;
                }
                else if (normalized == "add")
                {
                    selection.mode =
                        AssetViewer::SelectionMode::Add;
                }
                else if (normalized == "remove")
                {
                    selection.mode =
                        AssetViewer::SelectionMode::Remove;
                }
                else if (normalized == "toggle")
                {
                    selection.mode =
                        AssetViewer::SelectionMode::Toggle;
                }
                else
                {
                    return failure(
                        "mode must be replace, add, remove or toggle"
                    );
                }
                if (
                    const string* focus = find_any(
                        request,
                        { "focus_asset_id", "focus_id" }
                    )
                )
                {
                    if (focus->empty())
                    {
                        return failure("focus_id cannot be empty");
                    }
                    selection.focus_id = *focus;
                }

                string error;
                if (!viewer->SetSelection(selection, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_set_display",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                AssetViewer::DisplayRequest display;
                if (const string* value = find(request, "shading"))
                {
                    display.shading = as_shading(*value);
                    if (!display.shading)
                    {
                        return failure(
                            "shading must be solid, wire or vertices"
                        );
                    }
                }
                if (const string* value = find(request, "backdrop"))
                {
                    display.backdrop = as_backdrop(*value);
                    if (!display.backdrop)
                    {
                        return failure(
                            "backdrop must be auto, sky, charcoal, slate or paper"
                        );
                    }
                }

                string error;
                if (
                    !read_bool_value(
                        find_any(
                            request,
                            { "show_stats", "stats" }
                        ),
                        "show_stats",
                        display.show_stats,
                        error
                    ) ||
                    !read_bool(
                        request,
                        "auto_rotate",
                        display.auto_rotate,
                        error
                    )
                )
                {
                    return failure(error);
                }
                optional<bool> frame;
                optional<bool> reset;
                if (
                    !read_bool(request, "frame", frame, error) ||
                    !read_bool_value(
                        find_any(
                            request,
                            { "reset_camera", "reset" }
                        ),
                        "reset_camera",
                        reset,
                        error
                    )
                )
                {
                    return failure(error);
                }
                display.frame = frame.value_or(false);
                display.reset = reset.value_or(false);
                if (const string* value = find(request, "preview_lod"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (
                        !parsed ||
                        *parsed >
                            static_cast<uint64_t>(
                                numeric_limits<int>::max()
                            )
                    )
                    {
                        return failure(
                            "preview_lod must be a non-negative integer"
                        );
                    }
                    display.preview_lod =
                        static_cast<int>(*parsed);
                }

                if (!viewer->SetDisplay(display, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_preview_path",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const string* path = find(request, "path");
                if (!path || path->empty())
                {
                    return failure("path is required");
                }

                string error;
                if (!viewer->PreviewPath(*path, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_reload",
            [editor](const McpRequest&) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                string error;
                if (!viewer->Reload(error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_mesh",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const string* action = find(request, "action");
                if (!action)
                {
                    return failure("action is required");
                }

                AssetViewer::MeshRequest mesh;
                const string normalized = to_lower(*action);
                if (normalized == "simplify")
                {
                    mesh.action = AssetViewer::MeshAction::Simplify;
                }
                else if (normalized == "optimize")
                {
                    mesh.action = AssetViewer::MeshAction::Optimize;
                }
                else if (normalized == "build_lods")
                {
                    mesh.action = AssetViewer::MeshAction::BuildLods;
                }
                else if (normalized == "revert")
                {
                    mesh.action = AssetViewer::MeshAction::Revert;
                }
                else if (normalized == "set_options")
                {
                    mesh.action = AssetViewer::MeshAction::SetOptions;
                }
                else
                {
                    return failure(
                        "action must be simplify, optimize, build_lods, revert or set_options"
                    );
                }

                if (const string* value = find(request, "target_ratio"))
                {
                    mesh.target_ratio = as_float(value);
                    if (!mesh.target_ratio)
                    {
                        return failure(
                            "target_ratio must be a finite number"
                        );
                    }
                }
                string error;
                if (
                    !read_bool_value(
                        find_any(
                            request,
                            {
                                "generate_lods_on_save",
                                "generate_lods"
                            }
                        ),
                        "generate_lods_on_save",
                        mesh.generate_lods,
                        error
                    )
                )
                {
                    return failure(error);
                }
                if (const string* value = find(request, "preview_lod"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (
                        !parsed ||
                        *parsed >
                            static_cast<uint64_t>(
                                numeric_limits<int>::max()
                            )
                    )
                    {
                        return failure(
                            "preview_lod must be a non-negative integer"
                        );
                    }
                    mesh.preview_lod = static_cast<int>(*parsed);
                }
                optional<bool> confirm;
                if (
                    !read_bool(
                        request,
                        "confirm",
                        confirm,
                        error
                    )
                )
                {
                    return failure(error);
                }
                mesh.confirm = confirm.value_or(false);

                if (!viewer->EditMesh(mesh, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_mesh_save",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const optional<bool> confirm =
                    strict_bool(find(request, "confirm"));
                if (!confirm || !*confirm)
                {
                    return failure(
                        "confirm=true is required to overwrite the mesh"
                    );
                }

                string error;
                if (!viewer->SaveMesh(true, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_rename",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const string* asset_id = find(request, "asset_id");
                const string* linked_path = find_any(
                    request,
                    { "path", "linked_path" }
                );
                const string* new_name = find(request, "new_name");
                if (!new_name || new_name->empty())
                {
                    return failure("new_name is required");
                }

                string error;
                if (
                    !viewer->Rename(
                        asset_id ? *asset_id : "",
                        linked_path ? *linked_path : "",
                        *new_name,
                        error
                    )
                )
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_delete",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const optional<bool> confirm =
                    strict_bool(find(request, "confirm"));
                if (!confirm || !*confirm)
                {
                    return failure(
                        "confirm=true is required to delete assets"
                    );
                }

                const vector<string> asset_ids = string_list(
                    find_any(
                        request,
                        { "asset_ids", "ids", "asset_id" }
                    )
                );
                const string* linked_path = find_any(
                    request,
                    { "path", "linked_path" }
                );
                string error;
                if (
                    !viewer->Delete(
                        asset_ids,
                        linked_path ? *linked_path : "",
                        true,
                        error
                    )
                )
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_cleanup_scan",
            [editor](const McpRequest&) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                string error;
                AssetViewer::CleanupSummary result;
                if (
                    !viewer->ScanCleanup(
                        result,
                        error
                    )
                )
                {
                    return failure(error);
                }
                const uint64_t total = result.orphan_files.size();
                return
                    "{\"ok\":true,\"total\":" +
                    to_string(total) +
                    ",\"generation\":" +
                    to_string(result.generation) +
                    ",\"bytes\":" +
                    to_string(result.bytes) +
                    ",\"orphan_files\":" +
                    string_array(result.orphan_files) +
                    "}";
            }
        );

        add(
            "asset_viewer_cleanup_apply",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }
                const optional<bool> confirm =
                    strict_bool(find(request, "confirm"));
                if (!confirm || !*confirm)
                {
                    return failure(
                        "confirm=true is required to apply cleanup"
                    );
                }

                string error;
                const optional<uint64_t> generation = as_uint(
                    find(request, "generation")
                );
                if (!generation || *generation == 0)
                {
                    return failure(
                        "generation from cleanup_scan is required"
                    );
                }
                if (
                    !viewer->ApplyCleanup(
                        *generation,
                        true,
                        error
                    )
                )
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_select",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                const string* query = find_any(
                    request,
                    { "asset_id", "id", "name" }
                );
                string error;
                if (
                    !viewer->SelectAsset(
                        query ? *query : "",
                        error
                    )
                )
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_preview_entity",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                const string* value = find(request, "id");
                if (!value)
                {
                    return failure("id is required");
                }
                const optional<uint64_t> id = as_uint(value);
                if (!id)
                {
                    return failure("invalid entity id");
                }

                string error;
                if (!viewer->PreviewEntityById(*id, error))
                {
                    return failure(error);
                }
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_set_view",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                AssetViewer::ViewRequest view;
                if (const string* value = find(request, "view"))
                {
                    view.view = as_view(*value);
                    if (!view.view)
                    {
                        return failure("invalid asset viewer view");
                    }
                }

                // a number that will not parse is refused rather than read as a zero, which would aim
                // the camera somewhere the caller never asked for
                const char* numbers =
                    "yaw, pitch and zoom must be finite numbers";
                if (const string* value = find(request, "yaw"))
                {
                    view.yaw = as_float(value);
                    if (!view.yaw)
                    {
                        return failure(numbers);
                    }
                }
                if (const string* value = find(request, "pitch"))
                {
                    view.pitch = as_float(value);
                    if (!view.pitch)
                    {
                        return failure(numbers);
                    }
                }
                if (const string* value = find(request, "zoom"))
                {
                    view.zoom = as_float(value);
                    if (!view.zoom)
                    {
                        return failure(numbers);
                    }
                }

                viewer->SetPreviewView(view);
                return status_reply(viewer);
            }
        );

        add(
            "asset_viewer_screenshot",
            [editor](const McpRequest& request) -> string
            {
                AssetViewer* viewer = editor->GetWidget<AssetViewer>();
                if (!viewer)
                {
                    return failure("the asset viewer is not available");
                }

                AssetViewer::CaptureRequest capture;
                capture.path = capture_path(
                    find_any(
                        request,
                        {
                            "path",
                            "filename",
                            "file",
                            "file_path",
                            "name"
                        }
                    )
                );

                const char* integers =
                    "screenshot dimensions must be integers";
                if (const string* value = find(request, "width"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (!parsed)
                    {
                        return failure(integers);
                    }
                    capture.width = static_cast<uint32_t>(*parsed);
                }
                if (const string* value = find(request, "height"))
                {
                    const optional<uint64_t> parsed = as_uint(value);
                    if (!parsed)
                    {
                        return failure(integers);
                    }
                    capture.height = static_cast<uint32_t>(*parsed);
                }

                // a capture defaults to a shaded sky render regardless of the panel display
                if (const string* value = find(request, "shading"))
                {
                    const optional<AssetViewer::PreviewShading> shading =
                        as_shading(*value);
                    if (!shading)
                    {
                        return failure(
                            "shading must be solid, wire or vertices"
                        );
                    }
                    capture.shading = *shading;
                }
                if (const string* value = find(request, "backdrop"))
                {
                    const optional<AssetViewer::PreviewBackdrop> backdrop =
                        as_backdrop(*value);
                    if (!backdrop)
                    {
                        return failure(
                            "backdrop must be auto, sky, charcoal, slate or paper"
                        );
                    }
                    capture.backdrop = *backdrop;
                }

                AssetViewer::CaptureResult result;
                string error;
                if (!viewer->CapturePreview(capture, result, error))
                {
                    return failure(error);
                }

                return
                    "{\"ok\":true,\"ready\":" +
                    string(boolean(result.ready)) +
                    ",\"async\":" +
                    string(boolean(!result.ready)) +
                    ",\"path\":" +
                    quote(capture.path) +
                    ",\"width\":" +
                    to_string(result.width) +
                    ",\"height\":" +
                    to_string(result.height) +
                    ",\"shading\":" +
                    quote(shading_name(capture.shading)) +
                    ",\"generation\":" +
                    to_string(
                        Renderer::GetSecondaryViewRequestGeneration()
                    ) +
                    "}";
            }
        );
    }
}
