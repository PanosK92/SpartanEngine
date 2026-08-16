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

//= INCLUDES =============================
#include "pch.h"
#include "Viewport.h"
#include "AssetBrowser.h"
#include "WorldViewer.h"
#include "Properties.h"
#include "TerrainEditor.h"
#include "rhi/RHI_Device.h"
#include "rendering/Renderer.h"
#include "rendering/Material.h"
#include "resource/ResourceCache.h"
#include "world/World.h"
#include "world/Entity.h"
#include "world/Prefab.h"
#include "world/components/Render.h"
#include "world/components/Physics.h"
#include "world/components/Camera.h"
#include "world/components/Terrain.h"
#include "geometry/Mesh.h"
#include "physics/PhysicsWorld.h"
#include "core/ThreadPool.h"
#include "math/Ray.h"
#include "math/Plane.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_TransformGizmo.h"
#include "Settings.h"
//========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    bool first_frame         = true;
    uint32_t width_previous  = 0;
    uint32_t height_previous = 0;

    // drag preview state, the entity is tracked by id so a deletion mid-drag cannot dangle on revert
    uint64_t             preview_entity_id            = 0;
    shared_ptr<Material> preview_original_material;
    bool                 preview_original_was_default = false;
    bool                 preview_drag_was_active      = false;

    // triangle precision picking, aabbs alone fail because a gltf scene wrapper swallows the whole world
    Entity* pick_entity_under_cursor()
    {
        Camera* camera = World::GetCamera();
        if (!camera)
        {
            return nullptr;
        }

        return camera->FindEntityUnderCursor();
    }

    void clear_preview_state()
    {
        preview_entity_id            = 0;
        preview_original_material.reset();
        preview_original_was_default = false;
    }

    void revert_material_preview()
    {
        if (preview_entity_id == 0)
        {
            return;
        }

        if (Entity* entity = World::GetEntityById(preview_entity_id))
        {
            if (Render* render = entity->GetComponent<Render>())
            {
                if (preview_original_was_default)
                {
                    render->SetDefaultMaterial();
                }
                else if (preview_original_material)
                {
                    render->SetMaterial(preview_original_material);
                }
            }
        }

        clear_preview_state();
    }

    void apply_material_preview(Entity* entity, const char* material_path)
    {
        if (!entity || !material_path || !*material_path)
        {
            return;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render)
        {
            return;
        }

        // remember what to restore, the default flag short-circuits the path lookup for engine-owned defaults
        bool was_default = render->IsUsingDefaultMaterial();
        shared_ptr<Material> original;
        if (Material* current = render->GetMaterial(); current && !was_default)
        {
            original = ResourceCache::GetByPath<Material>(current->GetResourceFilePath());
        }

        // cache-aware load, no-op if the material is already in the resource cache
        shared_ptr<Material> dragged = ResourceCache::Load<Material>(material_path);
        if (!dragged)
        {
            return;
        }

        render->SetMaterial(dragged);

        preview_entity_id            = entity->GetObjectId();
        preview_original_material    = original;
        preview_original_was_default = was_default;
    }

    // peek at the active drag-drop payload without accepting, returns the path if it is a material drag
    // returns nullptr when there is no active drag or the active drag is not a material payload
    const char* peek_material_drag_path()
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (!payload || !payload->IsDataType(ImGuiSp::GDragDropTypes[(int)ImGuiSp::DragPayloadType::Material].data()))
        {
            return nullptr;
        }

        if (payload->DataSize < static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
        {
            return nullptr;
        }

        const ImGuiSp::DragDropPayload* sp_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
        if (sp_payload->path[0] == '\0')
        {
            return nullptr;
        }

        return sp_payload->path;
    }

    const char* peek_model_drag_path()
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (!payload || !payload->IsDataType(ImGuiSp::GDragDropTypes[(int)ImGuiSp::DragPayloadType::Model].data()))
        {
            return nullptr;
        }

        if (payload->DataSize < static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
        {
            return nullptr;
        }

        const ImGuiSp::DragDropPayload* sp_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
        if (sp_payload->path[0] == '\0')
        {
            return nullptr;
        }

        return sp_payload->path;
    }

    struct ModelPlaceDrag
    {
        string path;
        uint64_t preview_id     = 0;
        float ground_lift       = 0.0f;
        bool load_started       = false;
        bool pending_commit     = false;
        bool drag_was_active    = false;
        bool has_hit            = false;
        Vector3 last_hit        = Vector3::Zero;
    };

    ModelPlaceDrag model_place;

    bool compute_place_hit(Camera* camera, Entity* ignore, Vector3& hit_out)
    {
        if (!camera)
        {
            return false;
        }

        const Ray& pick_ray = camera->ComputePickingRay();
        const Vector3 origin = pick_ray.GetStart();
        Vector3 direction    = pick_ray.GetDirection() - origin;
        if (direction.LengthSquared() <= 0.0f)
        {
            return false;
        }
        direction.Normalize();

        for (Entity* entity : World::GetEntities())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            Terrain* terrain = entity->GetComponent<Terrain>();
            if (!terrain)
            {
                continue;
            }

            Vector3 terrain_hit;
            if (terrain->Raycast(pick_ray, terrain_hit))
            {
                hit_out = terrain_hit;
                return true;
            }
        }

        PhysicsRaycastHit physics_hit;
        if (PhysicsWorld::RaycastStatic(origin, direction, 10000.0f, physics_hit, ignore))
        {
            hit_out = physics_hit.position;
            return true;
        }

        const Ray world_ray(origin, direction);
        const Plane ground(Vector3::Up, Vector3::Zero);
        if (world_ray.HitDistance(ground, &hit_out) != numeric_limits<float>::infinity())
        {
            return true;
        }

        hit_out = origin + direction * 10.0f;
        return true;
    }

    float compute_ground_lift(Entity* root)
    {
        if (!root)
        {
            return 0.0f;
        }

        BoundingBox combined;
        vector<Entity*> nodes;
        nodes.push_back(root);
        root->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            Render* render = node->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            render->UpdateAabb();
            combined.Merge(render->GetBoundingBox());
        }

        if (combined.GetMin().y > combined.GetMax().y)
        {
            return 0.0f;
        }

        return root->GetPosition().y - combined.GetMin().y;
    }

    void ensure_imported_collision(Entity* root)
    {
        if (!root)
        {
            return;
        }

        vector<Entity*> nodes;
        nodes.push_back(root);
        root->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            Render* render = node->GetComponent<Render>();
            if (!render || !render->GetMesh())
            {
                continue;
            }

            Physics* physics = node->GetComponent<Physics>();
            if (!physics)
            {
                physics = node->AddComponent<Physics>();
                physics->SetUseConvexHull(true);
                physics->SetBodyType(BodyType::Mesh);
                continue;
            }

            if (physics->GetBodyType() == BodyType::Max)
            {
                physics->SetUseConvexHull(true);
                physics->SetBodyType(BodyType::Mesh);
                continue;
            }

            // clone copies physics before render, so the first cook has no mesh
            if (physics->GetBodyType() == BodyType::Mesh)
            {
                physics->Rebuild();
            }
        }
    }

    void refresh_render_culling(Entity* root)
    {
        if (!root)
        {
            return;
        }

        vector<Entity*> nodes;
        nodes.push_back(root);
        root->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            if (Render* render = node->GetComponent<Render>())
            {
                render->RefreshForEditor();
            }
        }
    }

    void move_model_preview(const Vector3& hit)
    {
        Entity* entity = World::GetEntityById(model_place.preview_id);
        if (!entity)
        {
            return;
        }

        entity->SetPosition(hit + Vector3(0.0f, model_place.ground_lift, 0.0f));
        refresh_render_culling(entity);
        model_place.last_hit = hit;
        model_place.has_hit  = true;
    }

    void cancel_model_place()
    {
        if (model_place.preview_id != 0)
        {
            if (Entity* entity = World::GetEntityById(model_place.preview_id))
            {
                World::RemoveEntityImmediate(entity);
            }
        }

        model_place = ModelPlaceDrag{};
    }

    void commit_model_place(Editor* editor)
    {
        Entity* entity = World::GetEntityById(model_place.preview_id);
        if (!entity)
        {
            if (!model_place.path.empty())
            {
                model_place.pending_commit = true;
            }
            return;
        }

        refresh_render_culling(entity);

        if (Camera* camera = World::GetCamera())
        {
            camera->SetSelectedEntity(entity);
        }

        if (editor)
        {
            editor->GetWidget<WorldViewer>()->SetSelectedEntity(entity);
        }

        model_place = ModelPlaceDrag{};
    }

    void spawn_model_preview_if_ready()
    {
        if (model_place.preview_id != 0 || model_place.path.empty())
        {
            return;
        }

        shared_ptr<Mesh> mesh = ResourceCache::GetByPath<Mesh>(model_place.path);
        if (!mesh || !mesh->GetRootEntity())
        {
            return;
        }

        Entity* source = mesh->GetRootEntity();
        Entity* preview = model_place.load_started ? source : source->Clone();

        if (!preview)
        {
            return;
        }

        preview->SetActive(true);

        const string mesh_dir = FileSystem::GetDirectoryFromFilePath(mesh->GetResourceFilePath());
        vector<Entity*> material_nodes;
        material_nodes.push_back(preview);
        preview->GetDescendants(&material_nodes);
        for (Entity* node : material_nodes)
        {
            Render* render = node->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (material && !render->IsUsingDefaultMaterial())
            {
                continue;
            }

            const string albedo_path = mesh_dir + "albedo.png";
            if (!FileSystem::Exists(albedo_path))
            {
                if (!material)
                {
                    render->SetDefaultMaterial();
                }
                continue;
            }

            if (!material || render->IsUsingDefaultMaterial())
            {
                shared_ptr<Material> created = make_shared<Material>();
                render->SetMaterial(created);
                material = render->GetMaterial();
            }

            if (!material)
            {
                render->SetDefaultMaterial();
                continue;
            }

            auto try_tex = [&](MaterialTextureType type, const char* file)
            {
                const string path = mesh_dir + file;
                if (FileSystem::Exists(path))
                {
                    material->SetTexture(type, path);
                }
            };
            try_tex(MaterialTextureType::Color,     "albedo.png");
            try_tex(MaterialTextureType::Normal,    "normal.png");
            try_tex(MaterialTextureType::Roughness, "roughness.png");
            try_tex(MaterialTextureType::Occlusion, "occlusion.png");
        }

        ensure_imported_collision(preview);
        refresh_render_culling(preview);
        model_place.preview_id  = preview->GetObjectId();
        model_place.ground_lift = compute_ground_lift(preview);

        if (model_place.has_hit)
        {
            move_model_preview(model_place.last_hit);
        }
    }

    void start_model_load_if_needed(const string& path)
    {
        if (ResourceCache::GetByPath<Mesh>(path))
        {
            return;
        }

        if (model_place.load_started)
        {
            return;
        }

        model_place.load_started = true;
        ThreadPool::AddTask([path]()
        {
            ResourceCache::Load<Mesh>(path, Mesh::GetDefaultFlags());
        });
    }

    void tick_model_place(Editor* editor, bool in_image)
    {
        const char* drag_path = peek_model_drag_path();
        const bool drag_active = drag_path != nullptr;

        if (drag_active)
        {
            if (model_place.path != drag_path)
            {
                cancel_model_place();
                model_place.path = drag_path;
            }

            start_model_load_if_needed(model_place.path);
            spawn_model_preview_if_ready();

            if (in_image)
            {
                Entity* ignore = World::GetEntityById(model_place.preview_id);
                Vector3 hit;
                if (compute_place_hit(World::GetCamera(), ignore, hit))
                {
                    move_model_preview(hit);
                }
            }

            model_place.drag_was_active = true;
            return;
        }

        spawn_model_preview_if_ready();

        if (model_place.pending_commit)
        {
            if (model_place.preview_id != 0)
            {
                if (model_place.has_hit)
                {
                    move_model_preview(model_place.last_hit);
                }
                commit_model_place(editor);
            }
            return;
        }

        if (model_place.drag_was_active)
        {
            if (in_image)
            {
                commit_model_place(editor);
            }
            else
            {
                cancel_model_place();
            }
        }
    }
}

Viewport::Viewport(Editor* editor) : Widget(editor)
{
    m_title         = "Viewport";
    m_dock          = WidgetDock::Center;
    m_size_initial  = Vector2(400, 250);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding       = Vector2(2.0f);
}

void Viewport::OnTickVisible()
{
    // get viewport size
    uint32_t width  = static_cast<uint32_t>(ImGui::GetContentRegionAvail().x);
    uint32_t height = static_cast<uint32_t>(ImGui::GetContentRegionAvail().y);

    // update engine's viewport
    static bool resolution_set = Settings::HasLoadedUserSettingsFromFile();
    if (!first_frame) // during the first frame the viewport is not yet initialized (it's size will be something weird)
    {
        if (width_previous != width || height_previous != height)
        {
            if (RHI_Device::IsValidResolution(width, height))
            {
                Renderer::SetViewport(static_cast<float>(width), static_cast<float>(height));

                if (!resolution_set)
                {
                    // only set the render and output resolutions once
                    // they are expensive operations and we don't want to do it frequently
                    Renderer::SetResolutionOutput(width, height);

                    resolution_set = true;
                }

                width_previous  = width;
                height_previous = height;
            }
        }
    }
    first_frame = false;

    // let the input system know about the position of this viewport within the editor
    // this will allow the system to properly calculate a relative mouse position
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 main_viewport_pos = ImGui::GetMainViewport()->Pos;
    const Vector2 offset = Vector2(screen_pos.x - main_viewport_pos.x, screen_pos.y - main_viewport_pos.y);
    Input::SetEditorViewportOffset(offset);

    // publish the viewport's screen-space rect so other systems can snap overlays to it
    m_screen_position = Vector2(screen_pos.x, screen_pos.y);
    m_screen_size     = Vector2(static_cast<float>(width), static_cast<float>(height));

    // draw the image after a potential resolution change call has been made
    ImGuiSp::image(Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // cache the image rect for hover tests, isitemhovered can return false during drag-drop
    ImVec2 image_rect_min = ImGui::GetItemRectMin();
    ImVec2 image_rect_max = ImGui::GetItemRectMax();

    if (Engine::IsFlagSet(EngineMode::Playing))
    {
        const bool paused = Engine::IsFlagSet(EngineMode::Paused);
        const char* label = paused ? "Paused" : "Playing";
        const ImVec4 status_color = paused ? ImGui::Style::color_warning : ImGui::Style::color_ok;
        const ImVec4 border_color = ImGui::Style::color_accent_1;
        const ImVec2 text_size = ImGui::CalcTextSize(label);
        const ImVec2 padding = ImGui::EditorUi::scaled(
            ImVec2(9.0f, 4.0f)
        );
        const float dot_size = ImGui::EditorUi::scaled(6.0f);
        const float margin = ImGui::EditorUi::scaled(12.0f);
        const float badge_width =
            text_size.x +
            padding.x * 2.0f +
            dot_size +
            ImGui::EditorUi::scaled(6.0f);
        const ImVec2 badge_min(
            image_rect_max.x - margin - badge_width,
            image_rect_min.y + margin
        );
        const ImVec2 badge_max(
            badge_min.x + badge_width,
            badge_min.y + text_size.y + padding.y * 2.0f
        );
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRect(
            image_rect_min,
            image_rect_max,
            ImGui::EditorUi::color(
                ImGui::EditorUi::alpha(border_color, 0.72f)
            ),
            0.0f,
            ImGui::EditorUi::scaled(2.0f),
            0
        );
        draw_list->AddRectFilled(
            badge_min,
            badge_max,
            ImGui::EditorUi::color(
                ImGui::EditorUi::alpha(
                    ImGui::Style::color_panel,
                    0.90f
                )
            ),
            ImGui::EditorUi::scaled(8.0f)
        );
        draw_list->AddRect(
            badge_min,
            badge_max,
            ImGui::EditorUi::color(
                ImGui::EditorUi::alpha(status_color, 0.48f)
            ),
            ImGui::EditorUi::scaled(8.0f)
        );
        const ImVec2 dot_center(
            badge_min.x + padding.x + dot_size * 0.5f,
            (badge_min.y + badge_max.y) * 0.5f
        );
        draw_list->AddCircleFilled(
            dot_center,
            dot_size * 0.5f,
            ImGui::EditorUi::color(status_color)
        );
        draw_list->AddText(
            ImVec2(
                dot_center.x +
                dot_size * 0.5f +
                ImGui::EditorUi::scaled(6.0f),
                badge_min.y + padding.y
            ),
            ImGui::EditorUi::color(
                ImGui::Style::color_text
            ),
            label
        );
    }

    // let the input system know if the mouse is within the viewport
    Input::SetMouseIsInViewport(ImGui::IsItemHovered());

    // material drag-preview, runs before the drop handlers so the drop just clears state without reverting
    {
        const char* drag_path  = peek_material_drag_path();
        bool        drag_active = drag_path != nullptr;
        bool        in_image   = ImGui::IsMouseHoveringRect(image_rect_min, image_rect_max);
        bool        previewing = drag_active && in_image;

        if (previewing)
        {
            Entity*  hovered    = pick_entity_under_cursor();
            uint64_t hovered_id = hovered ? hovered->GetObjectId() : 0;
            if (hovered_id != preview_entity_id)
            {
                revert_material_preview();
                if (hovered)
                {
                    apply_material_preview(hovered, drag_path);
                }
            }
        }
        else if (drag_active)
        {
            // drag still active but cursor moved off the image, restore the original
            revert_material_preview();
        }
        else if (preview_drag_was_active)
        {
            // the imgui drop handler can miss the release frame, so commit here instead of reverting
            clear_preview_state();
        }

        preview_drag_was_active = drag_active;
    }

    // model drag places a live preview under the cursor, import button still opens the dialog
    tick_model_place(m_editor, ImGui::IsMouseHoveringRect(image_rect_min, image_rect_max));
    if (ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Model))
    {
        commit_model_place(m_editor);
    }
    if ((model_place.preview_id != 0 || model_place.load_started) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        cancel_model_place();
    }

    // handle prefab drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Prefab))
    {
        if (payload->path[0] != '\0')
        {
            const char* file_path = payload->path;
            Entity* entity        = World::CreateEntity();
            string name           = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
            entity->SetObjectName(name);
            if (Prefab::LoadFromFile(file_path, entity))
            {
                entity->SetPrefabFilePath(file_path);

                // snapshot the loaded hierarchy as the prefab base so later edits persist as overrides
                entity->MarkPrefabBaseline();

                Vector3 hit;
                if (compute_place_hit(World::GetCamera(), entity, hit))
                {
                    const float lift = compute_ground_lift(entity);
                    entity->SetPosition(hit + Vector3(0.0f, lift, 0.0f));
                }
            }
            else
            {
                World::RemoveEntity(entity);
            }
        }
    }

    // handle material drop, the preview already applied the material to the hovered mesh,
    // so commit just means clearing the preview state without restoring the original
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Material))
    {
        if (preview_entity_id != 0)
        {
            clear_preview_state();
        }
        else if (payload->path[0] != '\0')
        {
            // fallback for the unlikely case the drop fires without a prior hover frame
            if (Entity* hovered = pick_entity_under_cursor())
            {
                if (Render* render = hovered->GetComponent<Render>())
                {
                    render->SetMaterial(payload->path);
                }
            }
        }
    }

    Camera* camera = World::GetCamera();

    // double-click to focus on entity
    if (camera && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        camera->Pick();
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
        if (camera->GetSelectedEntity())
        {
            camera->FocusOnSelectedEntity();
        }
    }
    // mouse picking (with multi-select via Ctrl handled in Pick())
    else if (camera && ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        camera->Pick();

        // when ctrl is held, Pick() already handled multi-selection via ToggleSelection(),
        // so we only update the properties panel without overwriting the camera's selection
        if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
        {
            Properties::ClearMaterialInspection();
        }
        else
        {
            m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
        }
    }

    // Ctrl+D to duplicate selected entities
    if (camera && ImGui::IsWindowFocused() && Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::D) && !ImGuiSp::editor_shortcuts_blocked())
    {
        const std::vector<Entity*>& selected_entities = camera->GetSelectedEntities();
        if (!selected_entities.empty())
        {
            // clone all selected entities
            std::vector<Entity*> cloned_entities;
            for (Entity* entity : selected_entities)
            {
                if (entity)
                {
                    Entity* cloned = entity->Clone();
                    if (cloned)
                    {
                        cloned_entities.push_back(cloned);
                    }
                }
            }

            // select the cloned entities instead
            if (!cloned_entities.empty())
            {
                camera->ClearSelection();
                for (Entity* cloned : cloned_entities)
                {
                    camera->AddToSelection(cloned);
                }
                m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(cloned_entities[0]);
            }
        }
    }

    // entity transform gizmo (shows when entities are selected and facing the camera)
    if (camera)
    {
        const std::vector<spartan::Entity*>& selected_entities = camera->GetSelectedEntities();
        if (!selected_entities.empty())
        {
            spartan::Entity* primary_selected = selected_entities[0];
            if (primary_selected)
            {
                spartan::Entity* camera_entity = camera->GetEntity();
                spartan::math::Vector3 dir_to_entity = primary_selected->GetPosition() - camera_entity->GetPosition();
                dir_to_entity.Normalize();
                if (dir_to_entity.Dot(camera_entity->GetForward()) >= 0.0f)
                {
                    ImGui::TransformGizmo::tick();
                }
            }
        }
    }

    // check if the engine wants cursor control
    if (camera && camera->GetFlag(spartan::CameraFlags::IsControlled))
    {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}
