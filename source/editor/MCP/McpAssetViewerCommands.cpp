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
#include "Widgets/AssetViewer.h"
#include "FileSystem/FileSystem.h"
#include "Rendering/Renderer.h"
#include "World/World.h"
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
                World::GetGeneratedResourceDirectory() +
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
                ",\"selected_version_id\":" +
                quote(status.selected_version_id) +
                ",\"loaded_path\":" +
                quote(status.loaded_path) +
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
                ",\"renderer_ready\":" +
                string(boolean(Renderer::IsSecondaryViewReady())) +
                ",\"renderer_generation\":" +
                to_string(Renderer::GetSecondaryViewGeneration()) +
                ",\"vertex_count\":" +
                to_string(status.vertex_count) +
                ",\"index_count\":" +
                to_string(status.index_count) +
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
                const string* version = find_any(
                    request,
                    { "version_id", "version", "candidate_version" }
                );

                string error;
                if (
                    !viewer->SelectAsset(
                        query ? *query : "",
                        version ? *version : "",
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

                // a capture is a design review, so it defaults to a shaded render on the neutral
                // backdrop no matter what the panel is showing, the sky still lights the asset either way
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
