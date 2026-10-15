/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "TreeCreator.h"
#include "PreviewRig.h"
#include "../Editor.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Properties.h"
#include "../imgui/source/imgui_stdlib.h"
#include "core/Event.h"
#include "core/ThreadPool.h"
#include "file_system/FileSystem.h"
#include "geometry/Mesh.h"
#include "rendering/Material.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Texture.h"
#include "world/Entity.h"
#include "world/Prefab.h"
#include "world/World.h"
#include "world/components/Physics.h"
#include "world/components/Render.h"
#include <algorithm>
#include <cmath>
#include <fstream>
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    const char* preview_name = "tree_creator_preview";

    string preview_directory()
    {
        return string(ResourceCache::GetProjectDirectory()) + "previews/trees/";
    }

    string preview_texture_directory()
    {
        return preview_directory() + "textures/";
    }

    string thumbnail_path(const string& id)
    {
        return preview_directory() + id + ".png";
    }

    string export_root()
    {
        return string(ResourceCache::GetProjectDirectory()) + "models/trees/";
    }

    // file and folder names stay lowercase ascii so a prefab reads the same on every file system
    string sanitize(const string& text)
    {
        string output;
        for (const char c : text)
        {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
            {
                output += c;
            }
            else if (c >= 'A' && c <= 'Z')
            {
                output += static_cast<char>(c - 'A' + 'a');
            }
            else if (c == ' ' || c == '-' || c == '.')
            {
                output += '_';
            }
        }
        return output;
    }

    bool is_default(const trees::controls& c)
    {
        const trees::controls d;
        return c.age == d.age && c.leafiness == d.leafiness && c.branchiness == d.branchiness && c.wildness == d.wildness &&
               c.time == d.time && c.seed == d.seed && c.detail == d.detail;
    }

    ImU32 to_display(const Vector3& linear, const float alpha = 1.0f)
    {
        auto encode = [](const float v)
        {
            return static_cast<int>(clamp(powf(clamp(v, 0.0f, 1.0f), 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return IM_COL32(encode(linear.x), encode(linear.y), encode(linear.z), static_cast<int>(alpha * 255.0f));
    }

    // the terrain would otherwise lay grass on every branch that faces the sky
    void opt_out_of_terrain(Material* material)
    {
        material->SetProperty(MaterialProperty::TerrainBlend, 0.0f);
        material->SetProperty(MaterialProperty::TerrainCoating, 0.0f);
    }

    shared_ptr<Material> make_bark_material(const trees::species& recipe, const string& albedo, const string& normal, const string& roughness, const string& name)
    {
        shared_ptr<Material> material = make_shared<Material>();
        material->SetObjectName(name);
        opt_out_of_terrain(material.get());
        material->SetTexture(MaterialTextureType::Color, albedo);
        material->SetTexture(MaterialTextureType::Normal, normal);
        material->SetTexture(MaterialTextureType::Roughness, roughness);
        material->SetProperty(MaterialProperty::Roughness, 1.0f);
        material->SetProperty(MaterialProperty::Normal, 1.0f);
        material->SetProperty(MaterialProperty::Metalness, 0.0f);
        material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
        return material;
    }

    // the name carries _foliage too, a material file loaded without its flags still comes back two sided and translucent
    shared_ptr<Material> make_foliage_material(const trees::species& recipe, const string& albedo, const string& normal, const string& name)
    {
        shared_ptr<Material> material = make_shared<Material>();
        material->SetObjectName(name);
        opt_out_of_terrain(material.get());
        material->SetTexture(MaterialTextureType::Color, albedo);
        material->SetTexture(MaterialTextureType::Normal, normal);
        material->SetProperty(MaterialProperty::Roughness, 0.85f);
        material->SetProperty(MaterialProperty::Metalness, 0.0f);
        material->SetProperty(MaterialProperty::IsFoliage, 1.0f);
        material->SetProperty(MaterialProperty::SubsurfaceScattering, max(recipe.translucency, 0.35f));
        material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
        material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
        material->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
        return material;
    }

    TreeCreator::stats measure(const Mesh* mesh, const trees::result& tree, const float milliseconds)
    {
        TreeCreator::stats output;
        output.generate_ms = milliseconds;
        output.budget      = tree.budget;
        output.height      = tree.height;
        output.crown_width = max(tree.bounds.GetSize().x, tree.bounds.GetSize().z);
        output.cards       = static_cast<uint32_t>(tree.cards.size() + tree.fronds.size());
        for (const trees::stem& s : tree.stems)
        {
            output.stems += s.pruned ? 0 : 1;
        }
        if (!mesh)
        {
            return output;
        }

        uint32_t lod_count = 0;
        for (uint32_t i = 0; i < mesh->GetSubMeshCount(); i++)
        {
            lod_count = max(lod_count, static_cast<uint32_t>(mesh->GetSubMesh(i).lods.size()));
        }
        output.lod_triangles.assign(lod_count, 0);
        for (uint32_t i = 0; i < mesh->GetSubMeshCount(); i++)
        {
            const vector<MeshLod>& lods = mesh->GetSubMesh(i).lods;
            if (lods.empty())
            {
                continue;
            }
            const uint32_t lod0 = lods[0].index_count / 3;
            output.triangles += lod0;
            output.vertices  += lods[0].vertex_count;
            (i == 0 ? output.bark_triangles : output.foliage_triangles) = lod0;
            for (uint32_t k = 0; k < lod_count; k++)
            {
                output.lod_triangles[k] += lods[min(k, static_cast<uint32_t>(lods.size()) - 1)].index_count / 3;
            }
        }
        return output;
    }
}

TreeCreator::TreeCreator(Editor* editor) : Widget(editor)
{
    m_title         = "Tree Creator";
    m_visible       = false;
    m_toolbar_order = 9;
    m_toolbar_icon  = static_cast<int>(IconType::Tree);
    m_size_initial  = Vector2(1360.0f, 800.0f);
    m_size_min      = Vector2(940.0f, 540.0f);
    m_next_request  = chrono::steady_clock::now();

    // the world takes the preview entities and the textures the materials point at with it
    m_world_unloading_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, SP_EVENT_HANDLER(OnWorldUnloading));
}

TreeCreator::~TreeCreator()
{
    if (m_world_unloading_handle != 0)
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnloading, m_world_unloading_handle);
        m_world_unloading_handle = 0;
    }
    if (m_job_future.valid())
    {
        m_job_future.wait();
    }
}

void TreeCreator::OnWorldUnloading()
{
    Renderer::InvalidateSecondaryView();
    m_preview_root_id   = 0;
    m_preview_rig_id    = 0;
    m_preview_camera_id = 0;
    m_mesh.reset();
    m_lod_meshes.clear();
    m_bark_material.reset();
    m_foliage_material.reset();
    m_ground_material.reset();
    m_thumbnails.clear();
    m_tree_key.clear();
    if (m_selected >= 0)
    {
        m_generate_pending = true;
        m_generate_after   = chrono::steady_clock::now() + chrono::milliseconds(500);
    }
}

void TreeCreator::SetPanelVisible(const bool visible)
{
    m_visible = visible;
    if (visible)
    {
        OnVisible();
    }
}

void TreeCreator::OnVisible()
{
    if (m_species.empty())
    {
        ReloadSpecies();
    }
    if (m_selected < 0 && !m_species.empty())
    {
        // greece is first in the region order, its olive is the tree this window opens on
        SelectSpecies(m_species.front().id);
    }
    else if (!m_mesh)
    {
        RequestGenerate();
    }
    m_preview_dirty = true;
}

void TreeCreator::OnInvisible()
{
    m_orbiting = false;
}

string TreeCreator::ReloadSpecies()
{
    const string selected_id = GetSelectedSpecies() ? GetSelectedSpecies()->id : "";

    string errors;
    m_species     = trees::load_all_species(&errors);
    m_load_errors = errors;

    m_regions.clear();
    for (const trees::species& recipe : m_species)
    {
        if (find(m_regions.begin(), m_regions.end(), recipe.region) == m_regions.end())
        {
            m_regions.push_back(recipe.region);
        }
    }
    if (find(m_regions.begin(), m_regions.end(), m_region) == m_regions.end())
    {
        m_region = m_regions.empty() ? "" : m_regions.front();
    }

    m_selected = -1;
    for (size_t i = 0; i < m_species.size(); i++)
    {
        if (m_species[i].id == selected_id)
        {
            m_selected = static_cast<int>(i);
        }
    }
    return errors;
}

bool TreeCreator::SelectSpecies(const string& id)
{
    for (size_t i = 0; i < m_species.size(); i++)
    {
        if (m_species[i].id == id)
        {
            m_selected = static_cast<int>(i);
            m_region   = m_species[i].region;
            m_status.clear();
            RequestGenerate();
            m_generate_after = chrono::steady_clock::now();
            return true;
        }
    }
    return false;
}

const trees::species* TreeCreator::GetSelectedSpecies() const
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_species.size()))
    {
        return nullptr;
    }
    return &m_species[m_selected];
}

void TreeCreator::SetControls(const trees::controls& controls)
{
    m_controls = controls;
    RequestGenerate();
    m_generate_after = chrono::steady_clock::now();
}

void TreeCreator::SetPreviewView(const float yaw, const float pitch, const float zoom)
{
    m_yaw           = yaw;
    m_pitch         = clamp(pitch, -1.45f, 1.45f);
    m_zoom          = clamp(zoom, 0.2f, 8.0f);
    m_preview_dirty = true;
}

void TreeCreator::SetPreviewLod(const int lod)
{
    const int count = static_cast<int>(m_stats.lod_triangles.size());
    const int value = clamp(lod, 0, max(0, count - 1));
    if (value != m_preview_lod)
    {
        m_preview_lod = value;
        BuildPreviewScene();
    }
}

void TreeCreator::RequestGenerate()
{
    m_generate_pending = true;
    // a slider drag asks every frame, the tree is grown once the hand rests
    m_generate_after = chrono::steady_clock::now() + chrono::milliseconds(90);
}

void TreeCreator::StartJob()
{
    const trees::species* recipe = GetSelectedSpecies();
    if (!recipe || m_job_running)
    {
        return;
    }

    m_job              = make_shared<job>();
    m_job->recipe      = *recipe;
    m_job->controls    = m_controls;
    m_job_running      = true;
    m_generate_pending = false;

    shared_ptr<job> work = m_job;
    m_job_future = ThreadPool::AddTask([work]()
    {
        const auto start = chrono::steady_clock::now();
        work->tree        = trees::generate(work->recipe, work->controls);
        work->textures_ok = trees::ensure_textures(work->recipe, work->controls.time, preview_texture_directory(), work->textures);
        work->milliseconds = chrono::duration<float, milli>(chrono::steady_clock::now() - start).count();
    });
}

bool TreeCreator::CollectJob(const bool wait)
{
    if (!m_job_running)
    {
        return false;
    }
    if (!wait && m_job_future.wait_for(chrono::seconds(0)) != future_status::ready)
    {
        return false;
    }

    m_job_future.get();
    m_job_running = false;
    shared_ptr<job> finished = m_job;
    m_job.reset();
    ApplyJob(*finished);
    return true;
}

void TreeCreator::ApplyJob(job& finished)
{
    if (finished.tree.bark_lods.empty() || finished.tree.bark_lods.front().indices.empty())
    {
        m_status = "the species produced no geometry";
        return;
    }

    const auto start = chrono::steady_clock::now();
    m_mesh = trees::build_mesh(finished.tree, finished.recipe.id + "_tree_creator", false);
    m_lod_meshes.clear();
    const float mesh_ms = chrono::duration<float, milli>(chrono::steady_clock::now() - start).count();

    // the textures only change with the species and the season
    const string key = finished.recipe.id + "_" + trees::to_string(finished.controls.time) + "_" + finished.textures.leaf_albedo;
    if (key != m_tree_key || !m_bark_material || !m_foliage_material)
    {
        m_tree_key = key;
        if (finished.textures_ok)
        {
            m_bark_material    = make_bark_material(finished.recipe, finished.textures.bark_albedo, finished.textures.bark_normal, finished.textures.bark_roughness, finished.recipe.id + "_bark");
            m_foliage_material = make_foliage_material(finished.recipe, finished.textures.leaf_albedo, finished.textures.leaf_normal, finished.recipe.id + "_foliage");
        }
        else
        {
            m_status = "the bark and leaf textures could not be written to " + preview_texture_directory();
        }
    }
    if (m_bark_material && m_foliage_material)
    {
        m_bark_material->SetProperty(MaterialProperty::WindAnimation, m_wind ? 1.0f : 0.0f);
        m_foliage_material->SetProperty(MaterialProperty::WindAnimation, m_wind ? 1.0f : 0.0f);
    }

    m_stats = measure(m_mesh.get(), finished.tree, finished.milliseconds + mesh_ms);
    m_tree  = move(finished.tree);
    m_preview_lod = min(m_preview_lod, max(0, static_cast<int>(m_stats.lod_triangles.size()) - 1));
    BuildPreviewScene();

    if (is_default(finished.controls) && !FileSystem::Exists(thumbnail_path(finished.recipe.id)))
    {
        m_thumbnail_capture = finished.recipe.id;
    }
}

bool TreeCreator::GenerateNow(string& error)
{
    if (!GetSelectedSpecies())
    {
        error = "select a species first";
        return false;
    }

    CollectJob(true);
    StartJob();
    if (!CollectJob(true))
    {
        error = "the tree could not be generated";
        return false;
    }
    if (!m_mesh)
    {
        error = m_status.empty() ? "the tree could not be generated" : m_status;
        return false;
    }
    return true;
}

void TreeCreator::OnTick()
{
    CollectJob(false);

    if (m_generate_pending && !m_job_running && chrono::steady_clock::now() >= m_generate_after && GetSelectedSpecies())
    {
        StartJob();
    }

    // the first render of a species at its default controls becomes its tile
    if (!m_thumbnail_capture.empty() && m_settle == 0 && Renderer::IsSecondaryViewReady() && !Renderer::IsSecondaryScreenshotPending() &&
        GetSelectedSpecies() && GetSelectedSpecies()->id == m_thumbnail_capture && World::GetEntityById(m_preview_root_id))
    {
        string error;
        const string path = thumbnail_path(m_thumbnail_capture);
        FileSystem::CreateDirectory_(preview_directory());
        if (CapturePreview(path, 256, 256, error))
        {
            m_thumbnail_pending[m_thumbnail_capture] = chrono::steady_clock::now();
            m_thumbnails.erase(m_thumbnail_capture);
        }
        m_thumbnail_capture.clear();
    }
}

void TreeCreator::DestroyPreviewScene()
{
    Renderer::InvalidateSecondaryView();
    if (Entity* root = World::GetEntityById(m_preview_root_id))
    {
        World::RemoveEntityImmediate(root);
    }
    m_preview_root_id   = 0;
    m_preview_rig_id    = 0;
    m_preview_camera_id = 0;
    m_orbiting          = false;
}

Mesh* TreeCreator::PreviewMeshForLod()
{
    if (!m_mesh || m_preview_lod <= 0)
    {
        return m_mesh.get();
    }

    const uint32_t level = static_cast<uint32_t>(m_preview_lod);
    if (m_lod_meshes.size() <= level)
    {
        m_lod_meshes.resize(level + 1);
    }
    if (!m_lod_meshes[level])
    {
        // the renderer picks levels by distance, a scratch mesh holding only one level shows it up close
        shared_ptr<Mesh> scratch = make_shared<Mesh>();
        scratch->SetObjectName(m_mesh->GetObjectName() + "_lod" + to_string(level));
        scratch->SetFlags(static_cast<uint32_t>(MeshFlags::PostProcessPreserveLod0) | static_cast<uint32_t>(MeshFlags::PostProcessSkipCache));
        scratch->ReserveSubMeshes(m_mesh->GetSubMeshCount());
        for (uint32_t i = 0; i < m_mesh->GetSubMeshCount(); i++)
        {
            const uint32_t count = m_mesh->GetLodCount(i);
            vector<uint32_t> indices;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            if (count == 0 || !m_mesh->GetGeometryLod(i, min(level, count - 1), &indices, &vertices))
            {
                continue;
            }
            scratch->AddGeometry(vertices, indices, false, i, true);
        }
        scratch->CreateGpuBuffers();
        m_lod_meshes[level] = scratch;
    }
    return m_lod_meshes[level].get();
}

void TreeCreator::BuildPreviewScene()
{
    DestroyPreviewScene();
    Mesh* mesh = PreviewMeshForLod();
    if (!mesh || !m_bark_material || !m_foliage_material)
    {
        return;
    }

    Entity* root = World::CreateEntity();
    root->SetObjectName(preview_name);
    root->SetTransient(true);
    m_preview_root_id = root->GetObjectId();

    auto add_part = [root](const char* name, Mesh* part_mesh, const uint32_t sub_mesh, const shared_ptr<Material>& material)
    {
        Entity* part = World::CreateEntity();
        part->SetObjectName(name);
        part->SetTransient(true);
        part->SetParent(root);
        Render* render = part->AddComponent<Render>();
        render->SetMesh(part_mesh, sub_mesh);
        render->SetMaterial(material);
        return part;
    };
    add_part("bark", mesh, 0, m_bark_material);
    if (mesh->GetSubMeshCount() > 1)
    {
        add_part("foliage", mesh, 1, m_foliage_material);
    }

    // a patch of ground, a tree floating in the sky reads as a model rather than as a tree
    if (!m_ground_material)
    {
        m_ground_material = make_shared<Material>();
        m_ground_material->SetObjectName("tree_creator_ground");
        opt_out_of_terrain(m_ground_material.get());
        m_ground_material->SetProperty(MaterialProperty::ColorR, 0.11f);
        m_ground_material->SetProperty(MaterialProperty::ColorG, 0.095f);
        m_ground_material->SetProperty(MaterialProperty::ColorB, 0.07f);
        m_ground_material->SetProperty(MaterialProperty::Roughness, 1.0f);
    }
    const Vector3 size        = m_tree.bounds.GetSize();
    const float ground_radius = max(max(size.x, size.z) * 0.75f, m_tree.height * 0.35f);
    {
        Entity* ground = World::CreateEntity();
        ground->SetObjectName("ground");
        ground->SetTransient(true);
        ground->SetParent(root);
        ground->SetPositionLocal(Vector3(m_tree.bounds.GetCenter().x, -0.02f, m_tree.bounds.GetCenter().z));
        ground->SetScaleLocal(Vector3(ground_radius * 2.0f, 0.04f, ground_radius * 2.0f));
        Render* render = ground->AddComponent<Render>();
        render->SetMesh(MeshType::Cylinder);
        render->SetMaterial(m_ground_material);
    }

    m_preview_center = m_tree.bounds.GetCenter();
    // the largest half extent, a sphere around the box diagonal leaves a tall narrow tree small in the frame
    const Vector3 extents = m_tree.bounds.GetExtents();
    m_preview_radius      = max(max(extents.y, max(extents.x, extents.z)) * 0.95f, 0.5f);

    root->SetActive(true);
    const preview_rig::rig_ids ids = preview_rig::create(root, m_preview_center, m_preview_radius, "tree_creator");
    m_preview_rig_id    = ids.rig;
    m_preview_camera_id = ids.camera;
    UpdatePreviewCamera();
    World::ProcessPendingAdditions();
    root->SetActive(false);

    m_settle        = 3;
    m_preview_dirty = true;
}

void TreeCreator::UpdatePreviewCamera()
{
    preview_rig::place_camera(World::GetEntityById(m_preview_camera_id), m_preview_center, m_preview_radius, m_yaw, m_pitch, m_zoom, m_preview_aspect);
}

bool TreeCreator::RequestPreviewRender(const uint32_t width, const uint32_t height)
{
    Entity* root   = World::GetEntityById(m_preview_root_id);
    Entity* camera = World::GetEntityById(m_preview_camera_id);
    if (!root || !camera)
    {
        return false;
    }

    m_preview_aspect = static_cast<float>(width) / static_cast<float>(max(1u, height));
    UpdatePreviewCamera();
    if (Renderer::RequestSecondaryView(camera, root, width, height, Renderer_SecondaryViewMode::Solid, Renderer_SecondaryViewBackdrop::Slate))
    {
        m_preview_dirty = false;
        m_next_request  = chrono::steady_clock::now() + chrono::milliseconds(16);
        return true;
    }
    return false;
}

bool TreeCreator::CapturePreview(const string& path, const uint32_t width, const uint32_t height, string& error)
{
    if (!World::GetEntityById(m_preview_root_id))
    {
        error = "generate a tree before capturing the preview";
        return false;
    }
    if (Renderer::IsSecondaryScreenshotPending())
    {
        error = "another preview capture is still pending, try again in a moment";
        return false;
    }

    // whoever waits for the file must not read the previous capture
    if (FileSystem::Exists(path))
    {
        FileSystem::Delete(path);
        if (FileSystem::Exists(path))
        {
            error = "the previous capture at this path is still being written";
            return false;
        }
    }
    const string directory = FileSystem::GetDirectoryFromFilePath(path);
    if (!directory.empty())
    {
        FileSystem::CreateDirectory_(directory);
    }

    // the capture is registered against a render and one more render is queued behind it, see AssetViewer::SavePreviewScreenshot
    const uint32_t w = clamp(width, 128u, 2048u);
    const uint32_t h = clamp(height, 128u, 2048u);
    if (!RequestPreviewRender(w, h) || !Renderer::ScreenshotSecondary(path))
    {
        error = "the preview could not be rendered";
        return false;
    }
    RequestPreviewRender(w, h);
    m_preview_dirty = true;
    return true;
}

RHI_Texture* TreeCreator::GetThumbnail(const string& id)
{
    auto found = m_thumbnails.find(id);
    if (found != m_thumbnails.end())
    {
        return found->second.get();
    }

    // a capture is written on a worker, the file is only trusted a moment after it was asked for
    auto pending = m_thumbnail_pending.find(id);
    if (pending != m_thumbnail_pending.end())
    {
        if (chrono::steady_clock::now() - pending->second < chrono::milliseconds(1500))
        {
            return nullptr;
        }
        m_thumbnail_pending.erase(pending);
    }

    const string path = thumbnail_path(id);
    shared_ptr<RHI_Texture> texture;
    if (FileSystem::Exists(path))
    {
        texture = make_shared<RHI_Texture>();
        texture->LoadFromFile(path);
        if (texture->GetWidth() > 0 && texture->GetHeight() > 0)
        {
            texture->PrepareForGpu();
        }
        else
        {
            texture.reset();
        }
    }
    m_thumbnails[id] = texture;
    return texture.get();
}

void TreeCreator::RefreshThumbnail(const string& id)
{
    m_thumbnails.erase(id);
}

bool TreeCreator::ExportOne(const trees::species& recipe, const trees::controls& controls, const string& name, export_result& output, string& error)
{
    trees::result tree = trees::generate(recipe, controls);
    if (tree.bark_lods.empty() || tree.bark_lods.front().indices.empty())
    {
        error = "the species produced no geometry";
        return false;
    }

    // a folder is never reused, the resource cache would hand back the mesh and materials of the previous export
    string folder    = name;
    string directory = export_root() + folder + "/";
    for (uint32_t suffix = 2; FileSystem::Exists(directory); suffix++)
    {
        folder    = name + "_" + to_string(suffix);
        directory = export_root() + folder + "/";
    }
    FileSystem::CreateDirectory_(directory + "textures/");

    trees::texture_paths source;
    if (!trees::ensure_textures(recipe, controls.time, preview_texture_directory(), source))
    {
        error = "the textures could not be generated";
        return false;
    }
    trees::texture_paths textures;
    textures.bark_albedo = directory + "textures/" + folder + "_bark.png";
    textures.bark_normal = directory + "textures/" + folder + "_bark_normal.png";
    textures.bark_roughness = directory + "textures/" + folder + "_bark_roughness.png";
    textures.leaf_albedo = directory + "textures/" + folder + "_leaf.png";
    textures.leaf_normal = directory + "textures/" + folder + "_leaf_normal.png";
    if (!FileSystem::CopyFileFromTo(source.bark_albedo, textures.bark_albedo) ||
        !FileSystem::CopyFileFromTo(source.bark_normal, textures.bark_normal) ||
        !FileSystem::CopyFileFromTo(source.bark_roughness, textures.bark_roughness) ||
        !FileSystem::CopyFileFromTo(source.leaf_albedo, textures.leaf_albedo) ||
        !FileSystem::CopyFileFromTo(source.leaf_normal, textures.leaf_normal))
    {
        error = "the textures could not be copied into " + directory;
        return false;
    }

    // baked with the impostor, saved, then loaded back so the prefab references exactly what a world will load
    const string mesh_path = directory + folder + EXTENSION_MESH;
    {
        shared_ptr<Mesh> built = trees::build_mesh(tree, folder, true);
        built->SetResourceFilePath(mesh_path);
        built->SaveToFile(mesh_path);
    }
    shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(mesh_path);
    if (!mesh || mesh->GetSubMeshCount() == 0)
    {
        error = "the mesh did not load back from " + mesh_path;
        return false;
    }

    const string bark_path    = directory + folder + "_bark" + EXTENSION_MATERIAL;
    const string foliage_path = directory + folder + "_foliage" + EXTENSION_MATERIAL;
    shared_ptr<Material> bark    = make_bark_material(recipe, textures.bark_albedo, textures.bark_normal, textures.bark_roughness, folder + "_bark");
    shared_ptr<Material> foliage = make_foliage_material(recipe, textures.leaf_albedo, textures.leaf_normal, folder + "_foliage");
    bark->SetResourceFilePath(bark_path);
    bark->SaveToFile(bark_path);
    foliage->SetResourceFilePath(foliage_path);
    foliage->SaveToFile(foliage_path);
    bark    = ResourceCache::Cache(bark);
    foliage = ResourceCache::Cache(foliage);

    Entity* root = World::CreateEntity();
    root->SetObjectName(folder);
    {
        Entity* part = World::CreateEntity();
        part->SetObjectName("bark");
        part->SetParent(root);
        Render* render = part->AddComponent<Render>();
        render->SetMesh(mesh.get(), 0);
        render->SetMaterial(bark);
    }
    if (mesh->GetSubMeshCount() > 1)
    {
        Entity* part = World::CreateEntity();
        part->SetObjectName("foliage");
        part->SetParent(root);
        Render* render = part->AddComponent<Render>();
        render->SetMesh(mesh.get(), 1);
        render->SetMaterial(foliage);
    }
    {
        // the capsule takes its size from the scale, two radii wide and as tall as the bare trunk
        const float clear  = max(tree.trunk_clear_height, tree.trunk_radius * 4.0f);
        const float radius = max(tree.trunk_radius, 0.03f);
        Entity* collider   = World::CreateEntity();
        collider->SetObjectName("trunk_collider");
        collider->SetParent(root);
        collider->SetPositionLocal(Vector3(0.0f, clear * 0.5f, 0.0f));
        collider->SetScaleLocal(Vector3(radius * 2.0f, clear, radius * 2.0f));
        Physics* physics = collider->AddComponent<Physics>();
        physics->SetStatic(true);
        physics->SetBodyType(BodyType::Capsule);
    }

    // the prefab writer skips children the world has not published yet
    World::ProcessPendingAdditions();
    const string prefab_path = directory + folder + EXTENSION_PREFAB;
    const bool saved         = Prefab::SaveToFile(root, prefab_path);
    World::RemoveEntityImmediate(root);
    if (!saved)
    {
        error = "the prefab could not be written to " + prefab_path;
        return false;
    }

    output.directory = directory;
    output.prefab    = prefab_path;
    output.mesh      = mesh_path;
    output.seed      = controls.seed;
    output.numbers   = measure(mesh.get(), tree, 0.0f);
    output.impostor  = mesh->GetSubMeshCount() > 1 && mesh->GetSubMesh(1).impostor != nullptr;
    return true;
}

bool TreeCreator::Export(const string& name, const uint32_t variations, vector<export_result>& results, string& error)
{
    const trees::species* recipe = GetSelectedSpecies();
    if (!recipe)
    {
        error = "select a species first";
        return false;
    }

    const uint32_t count = clamp(variations, 1u, 16u);
    const string base    = sanitize(name);
    for (uint32_t i = 0; i < count; i++)
    {
        trees::controls controls = m_controls;
        controls.seed            = m_controls.seed + i;
        string folder            = base.empty() ? recipe->id + "_" + to_string(controls.seed) : base;
        if (!base.empty() && count > 1)
        {
            folder += "_" + to_string(controls.seed);
        }

        export_result output;
        if (!ExportOne(*recipe, controls, folder, output, error))
        {
            return false;
        }
        results.push_back(output);
    }

    m_status = count == 1 ? "Exported " + results.back().prefab : "Exported " + to_string(count) + " trees to " + export_root();
    return true;
}

bool TreeCreator::SaveSpecies(const string& json, string& path, string& error)
{
    trees::species recipe;
    if (!trees::parse_species(json, recipe, error))
    {
        return false;
    }
    recipe.id = sanitize(recipe.id);
    if (recipe.id.empty())
    {
        error = "the species needs an id";
        return false;
    }
    if (recipe.region.empty())
    {
        error = "the species needs a region";
        return false;
    }

    const string directory = trees::species_directory() + "/" + sanitize(recipe.region) + "/";
    FileSystem::CreateDirectory_(directory);
    path = directory + recipe.id + ".json";
    ofstream file(path, ios::binary | ios::trunc);
    if (!file)
    {
        error = "could not write " + path;
        return false;
    }
    file << trees::serialize_species(recipe);
    file.close();

    ReloadSpecies();
    if (!SelectSpecies(recipe.id))
    {
        error = "the species was written but did not load back: " + m_load_errors;
        return false;
    }
    return true;
}

void TreeCreator::OnTickVisible()
{
    const float scale   = ImGui::EditorUi::scaled(1.0f);
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float gap     = 8.0f * scale;
    const float left    = clamp(region.x * 0.22f, 250.0f * scale, 330.0f * scale);
    const float right   = clamp(region.x * 0.22f, 270.0f * scale, 340.0f * scale);
    const float center  = max(200.0f * scale, region.x - left - right - gap * 2.0f);

    DrawLibrary(left, region.y);
    ImGui::SameLine(0.0f, gap);
    DrawPreview(center, region.y);
    ImGui::SameLine(0.0f, gap);
    DrawControls(right, region.y);
}

void TreeCreator::DrawLibrary(const float width, const float height)
{
    ImGui::BeginChild("##tree_library", ImVec2(width, height), ImGuiChildFlags_None);

    ImGui::PushFont(Editor::font_bold, 0.0f);
    ImGui::TextUnformatted("Regions");
    ImGui::PopFont();
    editor_ui::layout::caption("Real species, grown from their recipes");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    for (const string& region : m_regions)
    {
        uint32_t count = 0;
        for (const trees::species& recipe : m_species)
        {
            count += recipe.region == region ? 1 : 0;
        }

        ImGui::PushID(region.c_str());
        const bool selected = region == m_region;
        if (ImGui::Selectable("##region", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            m_region = region;
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float text_y = (min.y + max.y - ImGui::GetFontSize()) * 0.5f;
        draw->AddText(ImVec2(min.x + 8.0f, text_y), ImGui::EditorUi::color(selected ? ImGui::Style::color_text : ImGui::Style::color_text_muted), region.c_str());
        const string number = to_string(count);
        const float number_w = ImGui::CalcTextSize(number.c_str()).x;
        draw->AddText(ImVec2(max.x - number_w - 8.0f, text_y), ImGui::EditorUi::color(ImGui::Style::color_text_faint), number.c_str());
        ImGui::PopID();
    }

    if (m_species.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextWrapped("No species found in %s", trees::species_directory().c_str());
    }
    if (!m_load_errors.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_warning);
        ImGui::TextWrapped("%s", m_load_errors.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tile    = floorf((ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    uint32_t column     = 0;
    for (const trees::species& recipe : m_species)
    {
        if (recipe.region != m_region)
        {
            continue;
        }
        if (column % 2 == 1)
        {
            ImGui::SameLine(0.0f, spacing);
        }
        DrawTile(recipe, tile);
        column++;
    }

    ImGui::EndChild();
}

void TreeCreator::DrawSilhouette(const trees::species& recipe, const ImVec2& min, const ImVec2& max)
{
    ImDrawList* draw  = ImGui::GetWindowDrawList();
    const float w     = max.x - min.x;
    const float h     = max.y - min.y;
    const float cx    = (min.x + max.x) * 0.5f;
    const float floor = max.y - h * 0.08f;
    const ImU32 leaf  = to_display(recipe.leaf_color);
    const ImU32 bark  = to_display(recipe.bark_color);

    draw->AddRectFilledMultiColor(min, max, IM_COL32(64, 98, 140, 255), IM_COL32(64, 98, 140, 255), IM_COL32(150, 172, 190, 255), IM_COL32(150, 172, 190, 255));
    draw->AddRectFilled(ImVec2(min.x, floor), max, IM_COL32(52, 46, 36, 255));

    const float tree_h = h * 0.78f;
    const float top    = floor - tree_h;
    const float base   = floor - tree_h * recipe.crown_base;
    const float radius = clamp(recipe.crown_width, 0.15f, 0.6f) * tree_h * 0.75f;

    if (recipe.type == trees::tree_class::palm)
    {
        draw->AddLine(ImVec2(cx, floor), ImVec2(cx + w * 0.05f, top + tree_h * 0.1f), bark, 3.0f);
        for (int i = 0; i < 9; i++)
        {
            const float a = -2.6f + static_cast<float>(i) * 0.65f;
            draw->AddBezierQuadratic(ImVec2(cx + w * 0.05f, top + tree_h * 0.1f), ImVec2(cx + w * 0.05f + cosf(a) * radius * 0.8f, top + sinf(a) * radius * 0.3f), ImVec2(cx + w * 0.05f + cosf(a) * radius * 1.2f, top + tree_h * 0.3f), leaf, 3.0f);
        }
        return;
    }

    draw->AddLine(ImVec2(cx, floor), ImVec2(cx, base + (floor - base) * 0.1f), bark, std::max(2.0f, w * 0.03f));
    switch (recipe.shape)
    {
        case trees::crown_shape::conical:
        case trees::crown_shape::tend_flame:
        case trees::crown_shape::flame:
            draw->AddTriangleFilled(ImVec2(cx, top), ImVec2(cx + radius, base + (floor - base) * 0.2f), ImVec2(cx - radius, base + (floor - base) * 0.2f), leaf);
            break;
        case trees::crown_shape::cylindrical:
        case trees::crown_shape::tapered_cylindrical:
            draw->AddEllipseFilled(ImVec2(cx, (top + base) * 0.5f), ImVec2(radius * 0.55f, (base - top) * 0.55f), leaf);
            break;
        case trees::crown_shape::umbrella:
            draw->AddEllipseFilled(ImVec2(cx, top + (base - top) * 0.35f), ImVec2(radius * 1.25f, (base - top) * 0.35f), leaf);
            break;
        case trees::crown_shape::inverse_conical:
        case trees::crown_shape::vase:
            draw->AddTriangleFilled(ImVec2(cx - radius, top), ImVec2(cx + radius, top), ImVec2(cx, base + (floor - base) * 0.1f), leaf);
            draw->AddEllipseFilled(ImVec2(cx, top + (base - top) * 0.15f), ImVec2(radius, (base - top) * 0.2f), leaf);
            break;
        default:
            draw->AddEllipseFilled(ImVec2(cx, (top + base) * 0.5f), ImVec2(radius, (base - top) * 0.55f), leaf);
            break;
    }
}

void TreeCreator::DrawTile(const trees::species& recipe, const float size)
{
    ImGui::PushID(recipe.id.c_str());
    const float text_h = ImGui::GetFontSize();
    const ImVec2 total(size, size + text_h * 2.0f + 10.0f);
    const bool pressed = ImGui::InvisibleButton("##tile", total);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min   = ImGui::GetItemRectMin();
    const ImVec2 max   = ImGui::GetItemRectMax();
    ImDrawList* draw   = ImGui::GetWindowDrawList();

    const trees::species* selected = GetSelectedSpecies();
    const bool is_selected         = selected && selected->id == recipe.id;
    const float rounding           = 6.0f;

    draw->AddRectFilled(min, max, ImGui::EditorUi::color(hovered ? ImGui::Style::color_surface_hover : ImGui::Style::color_canvas_deep), rounding);
    const ImVec2 image_min(min.x + 4.0f, min.y + 4.0f);
    const ImVec2 image_max(max.x - 4.0f, min.y + size - 4.0f);
    if (RHI_Texture* thumbnail = GetThumbnail(recipe.id))
    {
        draw->AddImageRounded(reinterpret_cast<ImTextureID>(thumbnail), image_min, image_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding - 2.0f);
    }
    else
    {
        draw->PushClipRect(image_min, image_max, true);
        DrawSilhouette(recipe, image_min, image_max);
        draw->PopClipRect();
    }

    draw->PushClipRect(min, max, true);
    draw->AddText(Editor::font_bold, text_h, ImVec2(min.x + 6.0f, min.y + size), ImGui::EditorUi::color(ImGui::Style::color_text), recipe.name.c_str());
    draw->AddText(ImVec2(min.x + 6.0f, min.y + size + text_h + 2.0f), ImGui::EditorUi::color(ImGui::Style::color_text_faint), recipe.scientific.c_str());
    draw->PopClipRect();

    if (is_selected)
    {
        draw->AddRect(min, max, ImGui::EditorUi::color(ImGui::Style::color_accent_1), rounding, 2.0f);
    }
    if (pressed)
    {
        SelectSpecies(recipe.id);
    }
    if (hovered && !recipe.description.empty())
    {
        ImGuiSp::tooltip(recipe.description.c_str());
    }
    ImGui::PopID();
}

void TreeCreator::DrawPreview(const float width, const float height)
{
    ImGui::BeginChild("##tree_preview", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float scale = ImGui::EditorUi::scaled(1.0f);
    const ImVec2 size(max(1.0f, ImGui::GetContentRegionAvail().x), max(1.0f, ImGui::GetContentRegionAvail().y));
    const uint32_t preview_width  = max(1u, static_cast<uint32_t>(size.x));
    const uint32_t preview_height = max(1u, static_cast<uint32_t>(size.y));
    RHI_Texture* output           = Renderer::GetSecondaryViewOutput();
    const bool has_scene          = World::GetEntityById(m_preview_root_id) != nullptr;

    if (Renderer::IsSecondaryViewReady() && m_settle > 0)
    {
        m_settle--;
        m_preview_dirty = true;
    }
    if (has_scene && (!Renderer::IsSecondaryViewReady() || (output && (output->GetWidth() != preview_width || output->GetHeight() != preview_height))))
    {
        m_preview_dirty = true;
    }
    // the wind only moves while frames keep coming
    if (has_scene && m_wind)
    {
        m_preview_dirty = true;
    }

    if (has_scene && Renderer::IsSecondaryViewReady() && output)
    {
        ImGuiSp::image(output, size);
    }
    else
    {
        ImGui::Dummy(size);
    }
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw     = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(minimum);
    ImGui::InvisibleButton("##tree_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const ImGuiIO& io    = ImGui::GetIO();
    const bool hovered   = ImGui::IsItemHovered();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
    {
        m_orbiting = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_orbiting = false;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_zoom          = 1.0f;
        m_orbiting      = false;
        m_preview_dirty = true;
    }
    if (m_orbiting && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
    {
        m_yaw          -= io.MouseDelta.x * 0.012f;
        m_pitch         = clamp(m_pitch + io.MouseDelta.y * 0.012f, -1.45f, 1.45f);
        m_preview_dirty = true;
    }
    if (hovered && io.MouseWheel != 0.0f)
    {
        m_zoom          = clamp(m_zoom * (io.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.2f, 8.0f);
        m_preview_dirty = true;
    }

    if (!has_scene || !Renderer::IsSecondaryViewReady() || !output)
    {
        draw->AddRectFilled(minimum, maximum, IM_COL32(15, 18, 24, 255));
        const char* title = m_job_running || m_generate_pending ? "Growing tree" : "Pick a species";
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        draw->AddText(ImVec2((minimum.x + maximum.x - title_size.x) * 0.5f, (minimum.y + maximum.y - title_size.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title);
    }

    // top bar, wind and the level of detail on show
    {
        ImGui::SetCursorScreenPos(ImVec2(minimum.x + 10.0f * scale, minimum.y + 10.0f * scale));
        if (editor_ui::toolbar::pill("Wind", m_wind, ImGui::Style::color_accent_1, "Animate the tree in the wind"))
        {
            m_wind = !m_wind;
            if (m_bark_material && m_foliage_material)
            {
                m_bark_material->SetProperty(MaterialProperty::WindAnimation, m_wind ? 1.0f : 0.0f);
                m_foliage_material->SetProperty(MaterialProperty::WindAnimation, m_wind ? 1.0f : 0.0f);
            }
            m_preview_dirty = true;
        }
        for (int i = 0; i < static_cast<int>(m_stats.lod_triangles.size()); i++)
        {
            ImGui::SameLine(0.0f, 4.0f * scale);
            const string label   = "LOD " + to_string(i);
            const string tooltip = to_string(m_stats.lod_triangles[i]) + " triangles";
            if (editor_ui::toolbar::pill(label.c_str(), m_preview_lod == i, ImGui::Style::color_accent_1, tooltip.c_str()))
            {
                SetPreviewLod(i);
            }
        }
        if (!m_stats.lod_triangles.empty())
        {
            ImGui::SameLine(0.0f, 4.0f * scale);
            editor_ui::toolbar::pill("Impostor", false, ImGui::Style::color_text_muted, "The last level is a baked impostor card, it replaces the leaves of instanced trees in the distance and is built on export");
        }
    }

    // bottom left, the numbers that matter for a tree
    if (has_scene && m_mesh)
    {
        const uint32_t shown = m_stats.lod_triangles.empty() ? 0 : m_stats.lod_triangles[min(static_cast<size_t>(m_preview_lod), m_stats.lod_triangles.size() - 1)];
        char text[256];
        snprintf(text, sizeof(text), "%u triangles  (budget %u)    bark %u  leaves %u    %.1f m tall, %.1f m wide    %.0f ms",
            shown, m_stats.budget, m_stats.bark_triangles, m_stats.foliage_triangles, m_stats.height, m_stats.crown_width, m_stats.generate_ms);
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const ImVec2 card_min(minimum.x + 10.0f * scale, maximum.y - text_size.y - 22.0f * scale);
        const ImVec2 card_max(card_min.x + text_size.x + 16.0f * scale, maximum.y - 8.0f * scale);
        draw->AddRectFilled(card_min, card_max, IM_COL32(10, 13, 18, 190), 4.0f * scale);
        draw->AddText(ImVec2(card_min.x + 8.0f * scale, card_min.y + 7.0f * scale), IM_COL32(225, 230, 235, 230), text);
    }

    if (m_preview_dirty && has_scene && !Renderer::IsSecondaryScreenshotPending() && chrono::steady_clock::now() >= m_next_request)
    {
        RequestPreviewRender(preview_width, preview_height);
    }
    if (hovered && !m_orbiting)
    {
        ImGuiSp::tooltip("Drag to orbit\nWheel to zoom\nDouble click to reset");
    }

    ImGui::EndChild();
}

void TreeCreator::DrawControls(const float width, const float height)
{
    ImGui::BeginChild("##tree_controls", ImVec2(width, height), ImGuiChildFlags_None);

    const trees::species* recipe = GetSelectedSpecies();
    if (!recipe)
    {
        editor_ui::layout::caption("Pick a species on the left");
        ImGui::EndChild();
        return;
    }

    editor_ui::card_begin(recipe->name.c_str(), recipe->scientific.c_str());
    if (!recipe->description.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::TextWrapped("%s", recipe->description.c_str());
        ImGui::PopStyleColor();
    }
    editor_ui::chip(recipe->region.c_str(), ImGui::Style::color_accent_1);
    ImGui::SameLine();
    editor_ui::chip(recipe->evergreen ? "Evergreen" : "Deciduous", ImGui::Style::color_ok);
    editor_ui::card_end();

    bool changed = false;
    editor_ui::card_begin("Growth");
    changed |= editor_ui::property_percent("Age", &m_controls.age, "Sapling to ancient, height, girth and how far the crown has spread");
    changed |= editor_ui::property_percent("Leafiness", &m_controls.leafiness, "Sparse to dense foliage");
    changed |= editor_ui::property_percent("Branchiness", &m_controls.branchiness, "How many branches every level carries");
    changed |= editor_ui::property_percent("Wildness", &m_controls.wildness, "Tended and symmetric to wind beaten and gnarled");

    uint32_t season = static_cast<uint32_t>(m_controls.time);
    if (editor_ui::property_segmented("Season", { "Spring", "Summer", "Autumn", "Winter" }, &season, "Leaf colour, blossom and bare winter branches on deciduous trees"))
    {
        m_controls.time = static_cast<trees::season>(season);
        changed         = true;
    }

    editor_ui::layout::begin_property("Seed", "Every seed is a different tree of the same species");
    int seed = static_cast<int>(m_controls.seed);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.6f);
    if (ImGui::InputInt("##seed", &seed, 1, 10))
    {
        m_controls.seed = static_cast<uint32_t>(max(seed, 0));
        changed         = true;
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Roll"))
    {
        m_controls.seed = static_cast<uint32_t>(rand() % 99999) + 1;
        changed         = true;
    }

    uint32_t detail = static_cast<uint32_t>(m_controls.detail);
    if (editor_ui::property_segmented("Quality", { "Mobile", "Standard", "Hero" }, &detail, "Triangle budget, mobile is a third of standard, hero is twice"))
    {
        m_controls.detail = static_cast<trees::quality>(detail);
        changed           = true;
    }
    editor_ui::card_end();

    if (changed)
    {
        RequestGenerate();
    }

    editor_ui::card_begin("Export", "A prefab with bark, leaves, a trunk collider and an impostor for distant forests");
    const string hint = recipe->id + "_" + to_string(m_controls.seed);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##tree_name", hint.c_str(), &m_export_name);
    const float button_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    vector<export_result> results;
    string error;
    if (editor_ui::primary_button("Export", ImVec2(button_w, 0.0f)))
    {
        if (!Export(m_export_name, 1, results, error))
        {
            m_status = error;
        }
    }
    ImGui::SameLine();
    if (ImGuiSp::button("Export 4 variations", ImVec2(button_w, 0.0f)))
    {
        if (!Export(m_export_name, 4, results, error))
        {
            m_status = error;
        }
    }
    if (!m_status.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
        ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::PopStyleColor();
    }
    editor_ui::card_end();

    ImGui::EndChild();
}
