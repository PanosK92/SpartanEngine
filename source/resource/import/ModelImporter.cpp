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

//= INCLUDES ============================
#include "pch.h"
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include "ModelImporter.h"
#include "../../core/ProgressTracker.h"
#include "../../core/ThreadPool.h"
#include "../../rhi/RHI_Texture.h"
#include "../../geometry/Mesh.h"
#include "../../rendering/Material.h"
#include "../../animation/Animation.h"
#include "../../animation/Skeleton.h"
#include "../../animation/AnimationClip.h"
#include "../../animation/SkeletalMeshBinding.h"
#include "../../world/World.h"
#include "../../world/Entity.h"
#include "../../world/components/Light.h"
#include "../../world/components/Physics.h"
#include "../../world/components/Render.h"
#include "../../resource/ResourceCache.h"
SP_WARNINGS_OFF
#include "assimp/scene.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/version.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/anim.h"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace Assimp;
//============================

namespace spartan
{
    struct MeshJob
    {
        aiMesh*  assimp_mesh    = nullptr;
        Entity*  entity         = nullptr;
        // deterministic sub-mesh slot, assigned sequentially during ParseNode so the parallel ParseMesh stage cannot race on it
        uint32_t sub_mesh_index = 0;
    };

    struct ImportContext
    {
        string file_path;
        string model_name;
        string model_directory;
        Mesh* mesh           = nullptr;
        const aiScene* scene = nullptr;
        unordered_map<string, uint32_t> bone_name_to_index;
        unordered_map<string, string> directory_files;
        std::vector<MeshJob> mesh_jobs;
        std::mutex mesh_jobs_mutex;
    };

    namespace
    {
        // each ModelImporter::Load builds its own Assimp::Importer so concurrent imports are safe
        // and we don't need a global import lock anymore

        Matrix to_matrix(const aiMatrix4x4& transform)
        {
            return Matrix
            (
                transform.a1, transform.b1, transform.c1, transform.d1,
                transform.a2, transform.b2, transform.c2, transform.d2,
                transform.a3, transform.b3, transform.c3, transform.d3,
                transform.a4, transform.b4, transform.c4, transform.d4
            );
        }

        Color to_color(const aiColor4D& ai_color)
        {
            return Color(ai_color.r, ai_color.g, ai_color.b, ai_color.a);
        }

        Color to_color(const aiColor3D& ai_color)
        {
            return Color(ai_color.r, ai_color.g, ai_color.b, 1.0f);
        }

        Vector3 to_vector3(const aiVector3D& ai_vector)
        {
            return Vector3(ai_vector.x, ai_vector.y, ai_vector.z);
        }

        Quaternion to_quaternion(const aiQuaternion& ai_quaternion)
        {
            return Quaternion(ai_quaternion.x, ai_quaternion.y, ai_quaternion.z, ai_quaternion.w);
        }

        void set_entity_transform(const aiNode* node, Entity* entity)
        {
            const Matrix matrix_engine = to_matrix(node->mTransformation);
            entity->SetPositionLocal(matrix_engine.GetTranslation());
            entity->SetRotationLocal(matrix_engine.GetRotation());
            entity->SetScaleLocal(matrix_engine.GetScale());
        }

        uint32_t compute_node_count(const aiNode* node)
        {
            if (!node)
            {
                return 0;
            }

            uint32_t count = 1;
            for (uint32_t i = 0; i < node->mNumChildren; i++)
            {
                count += compute_node_count(node->mChildren[i]);
            }
            return count;
        }

        class AssimpProgress : public ProgressHandler
        {
        public:
            AssimpProgress(const string& file_path)
                : m_file_name(FileSystem::GetFileNameFromFilePath(file_path))
            {
            }

            bool Update(float percentage) override { return true; }

            void UpdateFileRead(int current_step, int number_of_steps) override
            {
                // reading progress is ignored - assimp doesn't call this consistently
            }

            void UpdatePostProcess(int current_step, int number_of_steps) override
            {
                if (current_step == 0)
                {
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(number_of_steps, "Post-processing model...");
                }
                else
                {
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
                }
            }

        private:
            string m_file_name;
        };

        string normalize_for_lookup(string path)
        {
            for (char& c : path)
            {
                if (c == '\\')
                {
                    c = '/';
                }
                else if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return path;
        }

        void build_directory_file_cache(const string& root, unordered_map<string, string>& out)
        {
            try
            {
                for (auto& entry : std::filesystem::recursive_directory_iterator(root))
                {
                    if (entry.is_regular_file())
                    {
                        string p = entry.path().string();
                        out.emplace(normalize_for_lookup(p), std::move(p));
                    }
                }
            }
            catch (...)
            {
                // directory missing or unreadable, fall back to FileSystem::Exists probes
            }
        }

        string resolve_texture_path(const string& original_path, const string& model_directory, const unordered_map<string, string>& directory_files)
        {
            auto probe = [&](const string& candidate) -> string
            {
                // try the directory cache first for o(1) hits on textures that live under model_directory
                if (!directory_files.empty())
                {
                    auto it = directory_files.find(normalize_for_lookup(candidate));
                    if (it != directory_files.end())
                    {
                        return it->second;
                    }
                }

                // fall back to a real filesystem check so we still resolve absolute paths and ../ traversals
                // that escape the cached subtree (some assets bake absolute paths or reference shared texture folders)
                return FileSystem::Exists(candidate) ? candidate : "";
            };

            // try the original path first (relative to model)
            string full_path = model_directory + original_path;
            if (string hit = probe(full_path); !hit.empty())
            {
                return hit;
            }

            // get base path without extension
            const string base_path = FileSystem::GetFilePathWithoutExtension(full_path);
            const string file_name_no_ext = FileSystem::GetFileNameWithoutExtensionFromFilePath(original_path);

            // common texture formats ordered by likelihood (case insensitive lookup handles .PNG/.JPG etc)
            static const array<const char*, 6> extensions = {
                ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds"
            };

            // try with different extensions
            for (const char* ext : extensions)
            {
                if (string hit = probe(base_path + ext); !hit.empty())
                {
                    return hit;
                }
            }

            // try in model directory (common for absolute paths baked by artists)
            for (const char* ext : extensions)
            {
                if (string hit = probe(model_directory + file_name_no_ext + ext); !hit.empty())
                {
                    return hit;
                }
            }

            return "";
        }

        // load texture synchronously (original working behavior)
        bool load_material_texture(
            const string& model_directory,
            const unordered_map<string, string>& directory_files,
            shared_ptr<Material> material,
            const aiMaterial* material_assimp,
            const MaterialTextureType texture_type,
            const aiTextureType texture_type_assimp_pbr,
            const aiTextureType texture_type_assimp_legacy
        )
        {
            // determine texture type (prefer pbr)
            aiTextureType type_assimp = aiTextureType_NONE;
            type_assimp = material_assimp->GetTextureCount(texture_type_assimp_pbr) > 0 ? texture_type_assimp_pbr : type_assimp;
            type_assimp = (type_assimp == aiTextureType_NONE) ? (material_assimp->GetTextureCount(texture_type_assimp_legacy) > 0 ? texture_type_assimp_legacy : type_assimp) : type_assimp;

            // check if the material has any textures
            if (material_assimp->GetTextureCount(type_assimp) == 0)
            {
                return true;
            }

            // get texture path
            aiString texture_path;
            if (material_assimp->GetTexture(type_assimp, 0, &texture_path) != AI_SUCCESS)
            {
                return false;
            }

            // resolve actual file path
            const string resolved_path = resolve_texture_path(texture_path.data, model_directory, directory_files);
            if (!FileSystem::IsSupportedImageFile(resolved_path))
            {
                return false;
            }

            // load the texture and set it to the material
            {
                shared_ptr<RHI_Texture> texture =
                    ResourceCache::GetByPath<RHI_Texture>(
                        resolved_path
                    );

                if (texture)
                {
                    material->SetTexture(texture_type, texture);
                }
                else
                {
                    material->SetTexture(texture_type, resolved_path);
                }
            }

            // fix: materials with diffuse texture should not be tinted black/gray
            if (type_assimp == aiTextureType_BASE_COLOR || type_assimp == aiTextureType_DIFFUSE)
            {
                material->SetProperty(MaterialProperty::ColorR, 1.0f);
                material->SetProperty(MaterialProperty::ColorG, 1.0f);
                material->SetProperty(MaterialProperty::ColorB, 1.0f);
                material->SetProperty(MaterialProperty::ColorA, 1.0f);
            }

            // fix: some models pass a normal map as a height map and vice versa
            if (texture_type == MaterialTextureType::Normal || texture_type == MaterialTextureType::Height)
            {
                if (RHI_Texture* texture = material->GetTexture(texture_type))
                {
                    MaterialTextureType proper_type = texture_type;
                    proper_type = (proper_type == MaterialTextureType::Normal && texture->IsGrayscale())  ? MaterialTextureType::Height : proper_type;
                    proper_type = (proper_type == MaterialTextureType::Height && !texture->IsGrayscale()) ? MaterialTextureType::Normal : proper_type;

                    if (proper_type != texture_type)
                    {
                        material->SetTexture(texture_type, nullptr);
                        material->SetTexture(proper_type, texture);
                    }
                }
            }

            return true;
        }

        bool assimp_material_has_textures(const aiMaterial* material_assimp)
        {
            if (!material_assimp)
            {
                return false;
            }

            static constexpr aiTextureType types[] =
            {
                aiTextureType_DIFFUSE,
                aiTextureType_BASE_COLOR,
                aiTextureType_NORMALS,
                aiTextureType_NORMAL_CAMERA,
                aiTextureType_METALNESS,
                aiTextureType_DIFFUSE_ROUGHNESS,
                aiTextureType_GLTF_METALLIC_ROUGHNESS,
                aiTextureType_AMBIENT_OCCLUSION,
                aiTextureType_LIGHTMAP,
                aiTextureType_EMISSIVE,
                aiTextureType_EMISSION_COLOR,
                aiTextureType_HEIGHT,
                aiTextureType_OPACITY
            };

            for (const aiTextureType type : types)
            {
                if (material_assimp->GetTextureCount(type) > 0)
                {
                    return true;
                }
            }

            return false;
        }

        // assimp invents defaultmaterial when the source file never assigned one
        bool is_undefined_assimp_material(const aiMaterial* material_assimp)
        {
            if (!material_assimp)
            {
                return true;
            }

            if (assimp_material_has_textures(material_assimp))
            {
                return false;
            }

            aiString name_assimp;
            aiGetMaterialString(material_assimp, AI_MATKEY_NAME, &name_assimp);
            string name = name_assimp.C_Str();
            transform(name.begin(), name.end(), name.begin(), ::tolower);

            // assimp uses defaultmaterial, a user material named default is still real
            return name.empty() || name == "defaultmaterial";
        }

        bool bind_directory_textures(ImportContext& ctx, const shared_ptr<Material>& material)
        {
            struct Hint
            {
                MaterialTextureType type;
                const char* names[4];
            };
            static constexpr Hint hints[] =
            {
                { MaterialTextureType::Color,     { "albedo", "diffuse", "color", "basecolor" } },
                { MaterialTextureType::Normal,    { "normal", "nor", "nrm", "normalmap" } },
                { MaterialTextureType::Roughness, { "roughness", "rough", "rgh", "" } },
                { MaterialTextureType::Occlusion, { "occlusion", "ao", "ambientocclusion", "" } }
            };

            bool bound_color = false;
            for (const Hint& hint : hints)
            {
                if (material->HasTextureOfType(hint.type))
                {
                    if (hint.type == MaterialTextureType::Color)
                    {
                        bound_color = true;
                    }
                    continue;
                }

                for (const char* name : hint.names)
                {
                    if (!name || name[0] == '\0')
                    {
                        continue;
                    }

                    const string path = resolve_texture_path(name, ctx.model_directory, ctx.directory_files);
                    if (path.empty())
                    {
                        continue;
                    }

                    material->SetTexture(hint.type, path);
                    if (hint.type == MaterialTextureType::Color)
                    {
                        bound_color = true;
                    }
                    break;
                }
            }

            return bound_color;
        }

        shared_ptr<Material> load_material(ImportContext& ctx, const aiMaterial* material_assimp)
        {
            SP_ASSERT(material_assimp != nullptr);
            shared_ptr<Material> material = make_shared<Material>();

            // each type writes its own m_textures slot so concurrent loads are safe, packing only runs once the material is complete
            struct TextureBinding
            {
                MaterialTextureType type;
                aiTextureType pbr;
                aiTextureType legacy;
            };
            static constexpr array<TextureBinding, 8> texture_bindings = {{
                { MaterialTextureType::Color,     aiTextureType_BASE_COLOR,              aiTextureType_DIFFUSE             },
                { MaterialTextureType::Roughness, aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS   },
                { MaterialTextureType::Metalness, aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_METALNESS           },
                { MaterialTextureType::Normal,    aiTextureType_NORMAL_CAMERA,           aiTextureType_NORMALS             },
                { MaterialTextureType::Occlusion, aiTextureType_AMBIENT_OCCLUSION,       aiTextureType_LIGHTMAP            },
                { MaterialTextureType::Emission,  aiTextureType_EMISSION_COLOR,          aiTextureType_EMISSIVE            },
                { MaterialTextureType::Height,    aiTextureType_HEIGHT,                  aiTextureType_NONE                },
                { MaterialTextureType::AlphaMask, aiTextureType_OPACITY,                 aiTextureType_NONE                },
            }};
            ThreadPool::ParallelLoop([&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    const TextureBinding& binding = texture_bindings[i];
                    load_material_texture(ctx.model_directory, ctx.directory_files, material, material_assimp, binding.type, binding.pbr, binding.legacy);
                }
            }, static_cast<uint32_t>(texture_bindings.size()));

            // gltf detection (including .glb binary format)
            const string extension = FileSystem::GetExtensionFromFilePath(ctx.file_path);
            const bool is_gltf = (extension == ".gltf") || (extension == ".glb");
            material->SetProperty(MaterialProperty::Gltf, is_gltf ? 1.0f : 0.0f);

            // name
            aiString name_assimp;
            aiGetMaterialString(material_assimp, AI_MATKEY_NAME, &name_assimp);
            string name = name_assimp.C_Str();
            material->SetResourceName(name + EXTENSION_MATERIAL);

            // color
            aiColor4D color_diffuse(1.0f, 1.0f, 1.0f, 1.0f);
            aiGetMaterialColor(material_assimp, AI_MATKEY_COLOR_DIFFUSE, &color_diffuse);

            // opacity
            aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
            aiGetMaterialColor(material_assimp, AI_MATKEY_OPACITY, &opacity);

            // convert name to lowercase once for all comparisons
            string name_lower = name;
            transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

            // detect transparency
            bool is_transparent = opacity.r < 1.0f;
            if (!is_transparent)
            {
                is_transparent =
                    name_lower.find("glass")       != string::npos ||
                    name_lower.find("transparent") != string::npos ||
                    name_lower.find("bottle")      != string::npos;
            }

            // set appropriate properties for transparents which are not pbr
            const bool has_roughness   = material->HasTextureOfType(MaterialTextureType::Roughness);
            const bool has_metalness   = material->HasTextureOfType(MaterialTextureType::Metalness);
            const bool is_pbr_material = has_roughness && has_metalness;
            if (is_transparent && !is_pbr_material)
            {
                opacity.r = 0.5f;
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
            }

            // set color and opacity
            material->SetProperty(MaterialProperty::ColorR, color_diffuse.r);
            material->SetProperty(MaterialProperty::ColorG, color_diffuse.g);
            material->SetProperty(MaterialProperty::ColorB, color_diffuse.b);
            material->SetProperty(MaterialProperty::ColorA, opacity.r);

            // gltf exporters flag opaque materials doubleSided as a precaution, only honor it for transparent and alpha tested surfaces
            const bool has_alpha_mask = material->HasTextureOfType(MaterialTextureType::AlphaMask);
            if (is_transparent || has_alpha_mask)
            {
                int no_culling = 1;
                aiGetMaterialInteger(material_assimp, AI_MATKEY_TWOSIDED, &no_culling);
                if (no_culling != 0)
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                }
            }

            // deduce metalness/roughness from material name if textures missing
            if (!has_metalness || !has_roughness)
            {
                const bool is_metal =
                    name_lower.find("metal")    != string::npos ||
                    name_lower.find("iron")     != string::npos ||
                    name_lower.find("radiator") != string::npos ||
                    name_lower.find("chrome")   != string::npos;

                const bool is_smooth  = name_lower.find("ceramic") != string::npos;
                const bool is_plaster = name_lower.find("plaster") != string::npos;
                const bool is_tile    = name_lower.find("tile")    != string::npos;

                if (!has_metalness && is_metal)
                {
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                if (!has_roughness)
                {
                    if (is_smooth || is_metal)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    }
                    else if (is_tile)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.4f);
                    }
                    else if (is_plaster)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.65f);
                    }
                }
            }

            return material;
        }

        // parallel vertex processing for large meshes
        void process_vertices_parallel(
            const aiMesh* assimp_mesh,
            vector<RHI_Vertex_PosTexNorTan>& vertices
        )
        {
            const uint32_t vertex_count = assimp_mesh->mNumVertices;
            vertices.resize(vertex_count);

            // use parallel loop for meshes with more than 10k vertices
            constexpr uint32_t parallel_threshold = 10000;

            if (vertex_count >= parallel_threshold)
            {
                ThreadPool::ParallelLoop([&](uint32_t start, uint32_t end)
                {
                    for (uint32_t i = start; i < end; i++)
                    {
                        RHI_Vertex_PosTexNorTan& vertex = vertices[i];

                        const aiVector3D& pos = assimp_mesh->mVertices[i];
                        vertex.pos[0] = pos.x;
                        vertex.pos[1] = pos.y;
                        vertex.pos[2] = pos.z;

                        if (assimp_mesh->mNormals)
                        {
                            const aiVector3D& n = assimp_mesh->mNormals[i];
                            vertex.set_normal(math::Vector3(n.x, n.y, n.z));
                        }

                        if (assimp_mesh->mTangents)
                        {
                            const aiVector3D& t = assimp_mesh->mTangents[i];
                            vertex.set_tangent(math::Vector3(t.x, t.y, t.z));
                        }

                        if (assimp_mesh->HasTextureCoords(0))
                        {
                            const auto& uv = assimp_mesh->mTextureCoords[0][i];
                            vertex.set_uv(uv.x, uv.y);
                        }
                    }
                }, vertex_count);
            }
            else
            {
                // sequential for small meshes (avoid thread overhead)
                for (uint32_t i = 0; i < vertex_count; i++)
                {
                    RHI_Vertex_PosTexNorTan& vertex = vertices[i];

                    const aiVector3D& pos = assimp_mesh->mVertices[i];
                    vertex.pos[0] = pos.x;
                    vertex.pos[1] = pos.y;
                    vertex.pos[2] = pos.z;

                    if (assimp_mesh->mNormals)
                    {
                        const aiVector3D& n = assimp_mesh->mNormals[i];
                        vertex.set_normal(math::Vector3(n.x, n.y, n.z));
                    }

                    if (assimp_mesh->mTangents)
                    {
                        const aiVector3D& t = assimp_mesh->mTangents[i];
                        vertex.set_tangent(math::Vector3(t.x, t.y, t.z));
                    }

                    if (assimp_mesh->HasTextureCoords(0))
                    {
                        const auto& tc = assimp_mesh->mTextureCoords[0][i];
                        vertex.set_uv(tc.x, tc.y);
                    }
                }
            }
        }

        // collect all unique bone names across all meshes in the scene
        void collect_bone_names(const aiScene* scene, vector<string>& out_names, unordered_map<string, uint32_t>& out_name_to_index)
        {
            out_names.clear();
            out_name_to_index.clear();

            for (uint32_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
            {
                const aiMesh* mesh = scene->mMeshes[mesh_idx];
                for (uint32_t bone_idx = 0; bone_idx < mesh->mNumBones; ++bone_idx)
                {
                    const string name = mesh->mBones[bone_idx]->mName.C_Str();
                    if (out_name_to_index.find(name) == out_name_to_index.end())
                    {
                        out_name_to_index[name] = static_cast<uint32_t>(out_names.size());
                        out_names.push_back(name);
                    }
                }
            }
        }

        // robotexpressive etc duplicate names (bone + mesh leaf). prefer armature bones.
        void collect_nodes_named(const aiNode* root, const string& name, vector<const aiNode*>& out)
        {
            if (!root)
            {
                return;
            }

            if (string(root->mName.C_Str()) == name)
            {
                out.push_back(root);
            }

            for (uint32_t i = 0; i < root->mNumChildren; ++i)
            {
                collect_nodes_named(root->mChildren[i], name, out);
            }
        }

        bool node_has_ancestor_named(const aiNode* node, const char* ancestor_name)
        {
            while (node)
            {
                if (string(node->mName.C_Str()) == ancestor_name)
                {
                    return true;
                }
                node = node->mParent;
            }
            return false;
        }

        int score_joint_node(const aiNode* node)
        {
            if (!node)
            {
                return -1000;
            }

            int score = 0;
            // bones have child joints/ends; mesh instances are usually leaves with meshes
            score += static_cast<int>(node->mNumChildren) * 10;
            if (node->mNumMeshes > 0 && node->mNumChildren == 0)
            {
                score -= 50;
            }
            if (node_has_ancestor_named(node, "Bone") || node_has_ancestor_named(node, "RobotArmature"))
            {
                score += 5;
            }
            return score;
        }

        const aiNode* find_node(const aiNode* root, const string& name)
        {
            vector<const aiNode*> matches;
            collect_nodes_named(root, name, matches);
            if (matches.empty())
            {
                return nullptr;
            }

            if (matches.size() == 1)
            {
                return matches[0];
            }

            const aiNode* best = matches[0];
            int best_score = score_joint_node(best);
            for (size_t i = 1; i < matches.size(); ++i)
            {
                const int score = score_joint_node(matches[i]);
                if (score > best_score)
                {
                    best_score = score;
                    best = matches[i];
                }
            }

            return best;
        }

        // engine world = local * parent_world
        Matrix compute_node_world(const aiNode* node)
        {
            if (!node)
            {
                return Matrix::Identity;
            }

            return to_matrix(node->mTransformation) * compute_node_world(node->mParent);
        }

        // build a skeleton from the scene's bone data
        // includes bone ancestors so skipped nodes are not lost, topo-sorted for pose eval
        shared_ptr<Skeleton> build_skeleton(
            const aiScene* scene,
            unordered_map<string, uint32_t>& out_name_to_index
        )
        {
            out_name_to_index.clear();

            vector<string> weighted_names;
            unordered_map<string, uint32_t> weighted_map;
            collect_bone_names(scene, weighted_names, weighted_map);

            if (weighted_names.empty())
            {
                return nullptr;
            }

            // include weighted bones, animated nodes, and all their ancestors
            unordered_set<string> joint_set(weighted_names.begin(), weighted_names.end());
            if (scene->mAnimations)
            {
                for (uint32_t a = 0; a < scene->mNumAnimations; ++a)
                {
                    const aiAnimation* anim = scene->mAnimations[a];
                    for (uint32_t c = 0; c < anim->mNumChannels; ++c)
                    {
                        const string channel_name = anim->mChannels[c]->mNodeName.C_Str();
                        if (!channel_name.empty())
                        {
                            joint_set.insert(channel_name);
                        }
                    }
                }
            }

            unordered_set<string> with_ancestors = joint_set;
            for (const string& name : joint_set)
            {
                const aiNode* node = find_node(scene->mRootNode, name);
                while (node)
                {
                    const string node_name = node->mName.C_Str();
                    if (!node_name.empty())
                    {
                        with_ancestors.insert(node_name);
                    }
                    node = node->mParent;
                }
            }
            joint_set = move(with_ancestors);

            // stable order: weighted bones first, then the rest
            vector<string> joint_names = weighted_names;
            for (const string& name : joint_set)
            {
                if (weighted_map.find(name) == weighted_map.end())
                {
                    joint_names.push_back(name);
                }
            }

            // resolve immediate parent joint for each joint
            const uint32_t unsorted_count = static_cast<uint32_t>(joint_names.size());
            vector<int32_t> unsorted_parents(unsorted_count, -1);
            unordered_map<string, uint32_t> unsorted_index;
            for (uint32_t i = 0; i < unsorted_count; ++i)
            {
                unsorted_index[joint_names[i]] = i;
            }

            for (uint32_t i = 0; i < unsorted_count; ++i)
            {
                const aiNode* node = find_node(scene->mRootNode, joint_names[i]);
                if (!node)
                {
                    continue;
                }

                const aiNode* parent = node->mParent;
                while (parent)
                {
                    auto it = unsorted_index.find(parent->mName.C_Str());
                    if (it != unsorted_index.end())
                    {
                        unsorted_parents[i] = static_cast<int32_t>(it->second);
                        break;
                    }
                    parent = parent->mParent;
                }
            }

            // topological sort, parents before children
            vector<uint32_t> depths(unsorted_count, 0);
            bool changed = true;
            for (uint32_t pass = 0; pass < unsorted_count && changed; ++pass)
            {
                changed = false;
                for (uint32_t i = 0; i < unsorted_count; ++i)
                {
                    if (unsorted_parents[i] < 0)
                    {
                        continue;
                    }

                    const uint32_t parent = static_cast<uint32_t>(unsorted_parents[i]);
                    const uint32_t depth = depths[parent] + 1;
                    if (depth > depths[i])
                    {
                        depths[i] = depth;
                        changed = true;
                    }
                }
            }

            vector<uint32_t> order(unsorted_count);
            for (uint32_t i = 0; i < unsorted_count; ++i)
            {
                order[i] = i;
            }
            stable_sort(order.begin(), order.end(), [&](const uint32_t a, const uint32_t b)
            {
                if (depths[a] != depths[b])
                {
                    return depths[a] < depths[b];
                }
                return a < b;
            });

            vector<string> sorted_names(unsorted_count);
            vector<int16_t> sorted_parents(unsorted_count, -1);
            unordered_map<uint32_t, uint32_t> old_to_new;
            for (uint32_t new_index = 0; new_index < unsorted_count; ++new_index)
            {
                const uint32_t old_index = order[new_index];
                old_to_new[old_index] = new_index;
                sorted_names[new_index] = joint_names[old_index];
            }

            for (uint32_t new_index = 0; new_index < unsorted_count; ++new_index)
            {
                const uint32_t old_index = order[new_index];
                const int32_t old_parent = unsorted_parents[old_index];
                if (old_parent >= 0)
                {
                    sorted_parents[new_index] = static_cast<int16_t>(old_to_new[static_cast<uint32_t>(old_parent)]);
                }
            }

            auto skeleton = make_shared<Skeleton>();
            const uint16_t joint_count = static_cast<uint16_t>(unsorted_count);
            skeleton->Allocate(joint_count);
            skeleton->joint_names = sorted_names;

            for (uint32_t i = 0; i < joint_count; ++i)
            {
                out_name_to_index[sorted_names[i]] = i;
                skeleton->m_mutable_parents[i] = sorted_parents[i];

                const aiNode* bone_node = find_node(scene->mRootNode, sorted_names[i]);
                if (!bone_node)
                {
                    skeleton->bind_local_matrices[i] = Matrix::Identity;
                    continue;
                }

                // prefer immediate node local when parent joint is the direct parent
                if (sorted_parents[i] >= 0)
                {
                    const aiNode* parent_joint_node = find_node(
                        scene->mRootNode,
                        sorted_names[static_cast<uint32_t>(sorted_parents[i])]
                    );

                    if (parent_joint_node && bone_node->mParent == parent_joint_node)
                    {
                        skeleton->bind_local_matrices[i] = to_matrix(bone_node->mTransformation);
                    }
                    else
                    {
                        // bake skipped nodes between parent joint and this joint
                        const Matrix bone_world = compute_node_world(bone_node);
                        const Matrix parent_world = parent_joint_node
                            ? compute_node_world(parent_joint_node)
                            : Matrix::Identity;
                        skeleton->bind_local_matrices[i] = bone_world * parent_world.Inverted();
                    }
                }
                else
                {
                    // root joint, use its node local only, not the full scene chain
                    skeleton->bind_local_matrices[i] = to_matrix(bone_node->mTransformation);
                }
            }

            skeleton->FinalizeBindPose();

            SP_LOG_INFO(
                "Skeleton built with %u joints (%u weighted)",
                joint_count,
                static_cast<uint32_t>(weighted_names.size())
            );

            return skeleton;
        }

        // extract bone weights for a single mesh into a SkeletalMeshSection
        void extract_bone_weights(
            const aiMesh* assimp_mesh,
            const unordered_map<string, uint32_t>& bone_name_to_index,
            const uint32_t sub_mesh_index,
            const uint32_t vertex_offset,
            SkeletalMeshBinding& binding)
        {
            if (assimp_mesh->mNumBones == 0)
            {
                return;
            }

            SkeletalMeshSection section;
            section.sub_mesh_index     = sub_mesh_index;
            section.vertex_input_offset = vertex_offset;
            section.vertex_count       = assimp_mesh->mNumVertices;
            section.influences.resize(assimp_mesh->mNumVertices);

            // track how many influences each vertex has accumulated
            vector<uint32_t> influence_counts(assimp_mesh->mNumVertices, 0);

            for (uint32_t bone_idx = 0; bone_idx < assimp_mesh->mNumBones; ++bone_idx)
            {
                const aiBone* bone = assimp_mesh->mBones[bone_idx];
                auto it = bone_name_to_index.find(bone->mName.C_Str());
                if (it == bone_name_to_index.end())
                {
                    continue;
                }

                const uint16_t global_bone_index = static_cast<uint16_t>(it->second);

                for (uint32_t weight_idx = 0; weight_idx < bone->mNumWeights; ++weight_idx)
                {
                    const aiVertexWeight& vw = bone->mWeights[weight_idx];
                    const uint32_t vertex_id = vw.mVertexId;

                    if (vertex_id >= assimp_mesh->mNumVertices)
                    {
                        continue;
                    }

                    SkeletalVertexInfluence& influence = section.influences[vertex_id];
                    uint32_t& count = influence_counts[vertex_id];

                    // store up to 4 influences per vertex
                    if (count < 4)
                    {
                        influence.bone_indices[count] = global_bone_index;
                        influence.bone_weights[count] = vw.mWeight;
                        ++count;
                    }
                }
            }

            // normalize weights so they sum to 1
            for (uint32_t v = 0; v < assimp_mesh->mNumVertices; ++v)
            {
                SkeletalVertexInfluence& influence = section.influences[v];
                float total = 0.0f;
                for (uint32_t w = 0; w < 4; ++w)
                {
                    total += influence.bone_weights[w];
                }

                if (total > 0.0f)
                {
                    const float inv = 1.0f / total;
                    for (uint32_t w = 0; w < 4; ++w)
                    {
                        influence.bone_weights[w] *= inv;
                    }
                }
            }

            // collect inverse bind matrices for bones used by this section
            section.inverse_bind_matrices.resize(bone_name_to_index.size(), Matrix::Identity);
            for (uint32_t bone_idx = 0; bone_idx < assimp_mesh->mNumBones; ++bone_idx)
            {
                const aiBone* bone = assimp_mesh->mBones[bone_idx];
                auto it = bone_name_to_index.find(bone->mName.C_Str());
                if (it != bone_name_to_index.end())
                {
                    section.inverse_bind_matrices[it->second] = to_matrix(bone->mOffsetMatrix);
                }
            }

            binding.AddSection(move(section));
        }

        Vector3 sample_vector_keys(const aiVectorKey* keys, const uint32_t count, const double time_ticks)
        {
            if (!keys || count == 0)
            {
                return Vector3::Zero;
            }

            if (count == 1 || time_ticks <= keys[0].mTime)
            {
                return to_vector3(keys[0].mValue);
            }

            if (time_ticks >= keys[count - 1].mTime)
            {
                return to_vector3(keys[count - 1].mValue);
            }

            uint32_t next = 1;
            while (next < count && time_ticks > keys[next].mTime)
            {
                ++next;
            }

            const uint32_t prev = next - 1;
            const double span = keys[next].mTime - keys[prev].mTime;
            const float t = span > 0.0
                ? static_cast<float>((time_ticks - keys[prev].mTime) / span)
                : 0.0f;

            return Vector3::Lerp(to_vector3(keys[prev].mValue), to_vector3(keys[next].mValue), t);
        }

        Quaternion sample_rotation_keys(const aiQuatKey* keys, const uint32_t count, const double time_ticks)
        {
            if (!keys || count == 0)
            {
                return Quaternion::Identity;
            }

            if (count == 1 || time_ticks <= keys[0].mTime)
            {
                return to_quaternion(keys[0].mValue);
            }

            if (time_ticks >= keys[count - 1].mTime)
            {
                return to_quaternion(keys[count - 1].mValue);
            }

            uint32_t next = 1;
            while (next < count && time_ticks > keys[next].mTime)
            {
                ++next;
            }

            const uint32_t prev = next - 1;
            const double span = keys[next].mTime - keys[prev].mTime;
            const float t = span > 0.0
                ? static_cast<float>((time_ticks - keys[prev].mTime) / span)
                : 0.0f;

            return Quaternion::Lerp(to_quaternion(keys[prev].mValue), to_quaternion(keys[next].mValue), t);
        }

        // convert an assimp animation to the engine's AnimationClip format
        AnimationClip convert_animation(
            const aiAnimation* anim,
            const unordered_map<string, uint32_t>& bone_name_to_index,
            const uint32_t joint_count,
            const string& clip_name)
        {
            AnimationClip clip;
            clip.name = clip_name;

            const double ticks_per_second = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;
            clip.duration_seconds = static_cast<float>(anim->mDuration / ticks_per_second);
            clip.sample_rate      = static_cast<float>(ticks_per_second);
            clip.joint_count      = joint_count;

            // densify to uniform samples so runtime can index by time * sample_rate
            const uint32_t frame_count = max(
                2u,
                static_cast<uint32_t>(std::lround(static_cast<double>(clip.duration_seconds) * static_cast<double>(clip.sample_rate))) + 1u
            );

            // initialize base pose to identity, evaluate starts from skeleton bind
            clip.base_local_positions.resize(joint_count, Vector3::Zero);
            clip.base_local_rotations.resize(joint_count, Quaternion::Identity);
            clip.base_local_scales.resize(joint_count, Vector3::One);

            for (uint32_t ch = 0; ch < anim->mNumChannels; ++ch)
            {
                const aiNodeAnim* channel = anim->mChannels[ch];
                const string channel_name = channel->mNodeName.C_Str();
                auto it = bone_name_to_index.find(channel_name);
                if (it == bone_name_to_index.end())
                {
                    continue;
                }

                const uint32_t bone_index = it->second;
                clip.sampled_bones.push_back(bone_index);

                // position keys
                if (channel->mNumPositionKeys == 1)
                {
                    ConstantPosition cp;
                    cp.bone_index = bone_index;
                    cp.value      = to_vector3(channel->mPositionKeys[0].mValue);
                    clip.position_stream.constants.push_back(cp);
                    clip.base_local_positions[bone_index] = cp.value;
                }
                else if (channel->mNumPositionKeys > 1)
                {
                    AnimChannel ac;
                    ac.bone_index   = bone_index;
                    ac.first_sample = static_cast<uint32_t>(clip.position_stream.values.size());
                    ac.sample_count = frame_count;
                    clip.position_stream.channels.push_back(ac);

                    for (uint32_t frame = 0; frame < frame_count; ++frame)
                    {
                        const double time_ticks = frame_count > 1
                            ? (static_cast<double>(frame) / static_cast<double>(frame_count - 1)) * anim->mDuration
                            : 0.0;
                        clip.position_stream.values.push_back(
                            sample_vector_keys(channel->mPositionKeys, channel->mNumPositionKeys, time_ticks)
                        );
                    }

                    clip.base_local_positions[bone_index] = clip.position_stream.values[ac.first_sample];
                }

                // rotation keys
                if (channel->mNumRotationKeys == 1)
                {
                    ConstantRotation cr;
                    cr.bone_index = bone_index;
                    cr.value      = to_quaternion(channel->mRotationKeys[0].mValue);
                    clip.rotation_stream.constants.push_back(cr);
                    clip.base_local_rotations[bone_index] = cr.value;
                }
                else if (channel->mNumRotationKeys > 1)
                {
                    AnimChannel ac;
                    ac.bone_index   = bone_index;
                    ac.first_sample = static_cast<uint32_t>(clip.rotation_stream.values.size());
                    ac.sample_count = frame_count;
                    clip.rotation_stream.channels.push_back(ac);

                    for (uint32_t frame = 0; frame < frame_count; ++frame)
                    {
                        const double time_ticks = frame_count > 1
                            ? (static_cast<double>(frame) / static_cast<double>(frame_count - 1)) * anim->mDuration
                            : 0.0;
                        clip.rotation_stream.values.push_back(
                            sample_rotation_keys(channel->mRotationKeys, channel->mNumRotationKeys, time_ticks)
                        );
                    }

                    clip.base_local_rotations[bone_index] = clip.rotation_stream.values[ac.first_sample];
                }

                // scale keys
                if (channel->mNumScalingKeys == 1)
                {
                    ConstantScale cs;
                    cs.bone_index = bone_index;
                    cs.value      = to_vector3(channel->mScalingKeys[0].mValue);
                    clip.scale_stream.constants.push_back(cs);
                    clip.base_local_scales[bone_index] = cs.value;
                }
                else if (channel->mNumScalingKeys > 1)
                {
                    AnimChannel ac;
                    ac.bone_index   = bone_index;
                    ac.first_sample = static_cast<uint32_t>(clip.scale_stream.values.size());
                    ac.sample_count = frame_count;
                    clip.scale_stream.channels.push_back(ac);

                    for (uint32_t frame = 0; frame < frame_count; ++frame)
                    {
                        const double time_ticks = frame_count > 1
                            ? (static_cast<double>(frame) / static_cast<double>(frame_count - 1)) * anim->mDuration
                            : 0.0;
                        clip.scale_stream.values.push_back(
                            sample_vector_keys(channel->mScalingKeys, channel->mNumScalingKeys, time_ticks)
                        );
                    }

                    clip.base_local_scales[bone_index] = clip.scale_stream.values[ac.first_sample];
                }
            }

            return clip;
        }

        // parallel index processing for large meshes
        void process_indices_parallel(
            const aiMesh* assimp_mesh,
            vector<uint32_t>& indices
        )
        {
            const uint32_t face_count = assimp_mesh->mNumFaces;
            const uint32_t index_count = face_count * 3;
            indices.resize(index_count);

            constexpr uint32_t parallel_threshold = 5000;

            if (face_count >= parallel_threshold)
            {
                ThreadPool::ParallelLoop([&](uint32_t start, uint32_t end)
                {
                    for (uint32_t face_index = start; face_index < end; face_index++)
                    {
                        const aiFace& face           = assimp_mesh->mFaces[face_index];
                        const uint32_t indices_index = face_index * 3;
                        indices[indices_index + 0]   = face.mIndices[0];
                        indices[indices_index + 1]   = face.mIndices[1];
                        indices[indices_index + 2]   = face.mIndices[2];
                    }
                }, face_count);
            }
            else
            {
                for (uint32_t face_index = 0; face_index < face_count; face_index++)
                {
                    const aiFace& face           = assimp_mesh->mFaces[face_index];
                    const uint32_t indices_index = face_index * 3;
                    indices[indices_index + 0]   = face.mIndices[0];
                    indices[indices_index + 1]   = face.mIndices[1];
                    indices[indices_index + 2]   = face.mIndices[2];
                }
            }
        }
    }

    void ModelImporter::Load(Mesh* mesh_in, const string& file_path)
    {
        SP_ASSERT_MSG(mesh_in != nullptr, "Invalid parameter");

        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("Provided file path doesn't point to an existing file");
            return;
        }

        // initialize import context
        ImportContext ctx;
        ctx.file_path       = file_path;
        ctx.model_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        ctx.model_directory = FileSystem::GetDirectoryFromFilePath(file_path);
        ctx.mesh            = mesh_in;
        ctx.mesh->SetObjectName(ctx.model_name);

        // walk the model directory once so resolve_texture_path does O(1) lookups instead of O(n) stat probes per material slot
        build_directory_file_cache(ctx.model_directory, ctx.directory_files);

        // set up the importer
        Importer importer;
        {
            // remove points and lines
            importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);

            // remove cameras
            importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS);

            // enable progress tracking
            importer.SetPropertyBool(AI_CONFIG_GLOB_MEASURE_TIME, true);
            importer.SetProgressHandler(new AssimpProgress(file_path));
        }

        // import flags
        uint32_t import_flags = 0;
        {
            import_flags |= aiProcess_ValidateDataStructure;
            import_flags |= aiProcess_Triangulate;
            import_flags |= aiProcess_SortByPType;

            // switch to engine conventions
            import_flags |= aiProcess_MakeLeftHanded;
            import_flags |= aiProcess_FlipUVs;
            import_flags |= aiProcess_FlipWindingOrder;

            // generate missing normals or uvs
            import_flags |= aiProcess_CalcTangentSpace;
            import_flags |= aiProcess_GenUVCoords;

            // generate missing normals, gltf/glb must ship them so regeneration is skipped
            // smooth stays behind a flag, otherwise use flat normals to keep hard edges
            const string extension        = FileSystem::GetExtensionFromFilePath(file_path);
            const bool source_has_normals = (extension == ".gltf") || (extension == ".glb");
            if (!source_has_normals)
            {
                if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportGenerateSmoothNormals))
                {
                    import_flags |= aiProcess_GenSmoothNormals;
                }
                else
                {
                    import_flags |= aiProcess_GenNormals;
                }
            }

            // limit bone weights to 4 per vertex
            import_flags |= aiProcess_LimitBoneWeights;

            // combine meshes
            if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportCombineMeshes))
            {
                import_flags |= aiProcess_OptimizeMeshes;
                import_flags |= aiProcess_PreTransformVertices;
            }

            // aiProcess_JoinIdenticalVertices is deliberately off, meshoptimizer welds identical vertices faster in optimize()
            if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportRemoveRedundantData))
            {
                import_flags |= aiProcess_RemoveRedundantMaterials;
                import_flags |= aiProcess_FindDegenerates;
                import_flags |= aiProcess_FindInvalidData;
                import_flags |= aiProcess_FindInstances;
            }
        }

        ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(1, "Loading model from drive...");

        // read the 3d model file from drive
        ctx.scene = importer.ReadFile(file_path, import_flags);
        if (ctx.scene)
        {
            // extract skeleton before parsing nodes so bone indices are available during mesh parsing
            ParseSkeleton(ctx);

            // update progress tracking
            const uint32_t job_count = compute_node_count(ctx.scene->mRootNode);
            ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(job_count, "Parsing model...");

            // recursively parse nodes (sequential, just creates entities and collects mesh jobs)
            ParseNode(ctx, ctx.scene->mRootNode);

            // sub-mesh slots are reserved up front so each parallel ParseMesh writes a deterministic index
            if (!ctx.mesh_jobs.empty())
            {
                const uint32_t mesh_job_count = static_cast<uint32_t>(ctx.mesh_jobs.size());
                ctx.mesh->ReserveSubMeshes(mesh_job_count);
                ThreadPool::ParallelLoop([&ctx](uint32_t start, uint32_t end)
                {
                    for (uint32_t i = start; i < end; i++)
                    {
                        const MeshJob& job = ctx.mesh_jobs[i];
                        ParseMesh(ctx, job.assimp_mesh, job.entity, job.sub_mesh_index);
                    }
                }, mesh_job_count);

                // cook after parse so render geometry exists, convex hull survives messy imported topology
                for (const MeshJob& job : ctx.mesh_jobs)
                {
                    if (!job.entity || job.entity->GetComponent<Physics>())
                    {
                        continue;
                    }

                    if (!job.entity->GetComponent<Render>())
                    {
                        continue;
                    }

                    Physics* physics = job.entity->AddComponent<Physics>();
                    physics->SetUseConvexHull(true);
                    physics->SetBodyType(BodyType::Mesh);
                }
            }

            // extract animation clips
            ParseAnimations(ctx);

            // update model geometry
            ctx.mesh->CreateGpuBuffers();

            // make the root entity active since it's now thread-safe
            ctx.mesh->GetRootEntity()->SetActive(true);
        }
        else
        {
            SP_LOG_ERROR("%s", importer.GetErrorString());
        }

        // always clear importer progress so a missed JobDone cannot wedge IsLoading forever
        ProgressTracker::GetProgress(ProgressType::ModelImporter).Complete();

        importer.FreeScene();
    }

    void ModelImporter::ParseNode(ImportContext& ctx, const aiNode* node, Entity* parent_entity)
    {
        // create an entity that will match this node
        Entity* entity = World::CreateEntity();

        // set root entity to mesh
        const bool is_root_node = parent_entity == nullptr;
        if (is_root_node)
        {
            ctx.mesh->SetRootEntity(entity);
            entity->SetActive(false);
        }

        // name the entity
        const string node_name = is_root_node ? ctx.model_name : node->mName.C_Str();
        entity->SetObjectName(node_name);

        // update progress tracking
        ProgressTracker::GetProgress(ProgressType::ModelImporter).SetText("Creating entity for " + entity->GetObjectName());

        // set parent
        entity->SetParent(parent_entity);

        // apply node transformation
        set_entity_transform(node, entity);

        // mesh components
        if (node->mNumMeshes > 0)
        {
            ParseNodeMeshes(ctx, node, entity);
        }

        // light component
        if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportLights))
        {
            ParseNodeLight(ctx, node, entity);
        }

        // children nodes
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ParseNode(ctx, node->mChildren[i], entity);
        }

        // update progress tracking
        ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
    }

    void ModelImporter::ParseNodeMeshes(ImportContext& ctx, const aiNode* assimp_node, Entity* node_entity)
    {
        SP_ASSERT_MSG(assimp_node->mNumMeshes != 0, "No meshes to process");

        for (uint32_t i = 0; i < assimp_node->mNumMeshes; i++)
        {
            Entity* entity     = node_entity;
            aiMesh* node_mesh  = ctx.scene->mMeshes[assimp_node->mMeshes[i]];
            string node_name   = assimp_node->mName.C_Str();

            // if this node has more than one mesh, create an entity for each
            if (assimp_node->mNumMeshes > 1)
            {
                entity = World::CreateEntity();
                entity->SetParent(node_entity);
                node_name += "_" + to_string(i + 1);
            }

            entity->SetObjectName(node_name);

            // collect the job, ParseMesh runs later in parallel after the tree walk completes,
            // the sub-mesh index is assigned here from the current jobs count so it's deterministic and matches assimp's traversal order
            const uint32_t deterministic_sub_mesh_index = static_cast<uint32_t>(ctx.mesh_jobs.size());
            ctx.mesh_jobs.push_back({ node_mesh, entity, deterministic_sub_mesh_index });
        }
    }

    void ModelImporter::ParseNodeLight(ImportContext& ctx, const aiNode* node, Entity* new_entity)
    {
        for (uint32_t i = 0; i < ctx.scene->mNumLights; i++)
        {
            if (ctx.scene->mLights[i]->mName == node->mName)
            {
                const aiLight* light_assimp = ctx.scene->mLights[i];

                Light* light = new_entity->AddComponent<Light>();

                // disable shadows (to avoid tanking the framerate)
                light->SetFlag(LightFlags::Shadows, false);
                light->SetFlag(LightFlags::Volumetric, false);

                // type first so constructor side effects do not leave an infinite/max bbox
                switch (light_assimp->mType)
                {
                    case aiLightSource_DIRECTIONAL:
                        light->SetLightType(LightType::Directional);
                        break;
                    case aiLightSource_POINT:
                    case aiLightSource_AREA:
                        light->SetLightType(LightType::Point);
                        break;
                    case aiLightSource_SPOT:
                        light->SetLightType(LightType::Spot);
                        break;
                    default:
                        light->SetLightType(LightType::Point);
                        break;
                }

                // local transform, only orient lights that have a meaningful direction
                light->GetEntity()->SetPositionLocal(to_vector3(light_assimp->mPosition));
                if (light_assimp->mType == aiLightSource_DIRECTIONAL || light_assimp->mType == aiLightSource_SPOT)
                {
                    const Vector3 direction = to_vector3(light_assimp->mDirection);
                    if (direction.LengthSquared() > epsilon)
                    {
                        light->GetEntity()->SetRotationLocal(Quaternion::FromLookRotation(direction));
                    }
                }

                // color
                light->SetColor(to_color(light_assimp->mColorDiffuse));
            }
        }
    }

    void ModelImporter::ParseMesh(ImportContext& ctx, aiMesh* assimp_mesh, Entity* entity_parent, const uint32_t sub_mesh_index)
    {
        SP_ASSERT(assimp_mesh != nullptr);
        SP_ASSERT(entity_parent != nullptr);

        // process vertices and indices (parallel for large meshes)
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        process_vertices_parallel(assimp_mesh, vertices);
        process_indices_parallel(assimp_mesh, indices);

        // serialize append + weight offsets, parallel ParseMesh raced GetVertexCount
        // and wrote overlapping skin sections (mannequiny exploded)
        static mutex geometry_append_mutex;
        uint32_t vertex_offset = 0;
        {
            lock_guard<mutex> geometry_lock(geometry_append_mutex);

            vertex_offset = ctx.mesh->GetVertexCount();
            ctx.mesh->AddGeometry(vertices, indices, true, sub_mesh_index);

            if (assimp_mesh->mNumBones > 0 && !ctx.bone_name_to_index.empty())
            {
                if (!ctx.mesh->GetSkeletalMeshBinding())
                {
                    ctx.mesh->SetSkeletalMeshBinding(make_unique<SkeletalMeshBinding>());
                }

                extract_bone_weights(
                    assimp_mesh,
                    ctx.bone_name_to_index,
                    sub_mesh_index,
                    vertex_offset,
                    *ctx.mesh->GetSkeletalMeshBinding()
                );
            }
        }

        // set the geometry
        Render* render = entity_parent->AddComponent<Render>();
        render->SetMesh(ctx.mesh, sub_mesh_index);

        // keep the imported material even when it has no albedo, glass and metals often dont
        const aiMaterial* assimp_material = nullptr;
        if (ctx.scene->HasMaterials() &&
            assimp_mesh->mMaterialIndex < ctx.scene->mNumMaterials)
        {
            assimp_material = ctx.scene->mMaterials[assimp_mesh->mMaterialIndex];
        }

        if (is_undefined_assimp_material(assimp_material))
        {
            shared_ptr<Material> fallback = make_shared<Material>();
            if (bind_directory_textures(ctx, fallback))
            {
                fallback->SetResourceName(entity_parent->GetObjectName() + string(EXTENSION_MATERIAL));
                fallback->SetResourceFilePath(ctx.model_directory + fallback->GetObjectName() + EXTENSION_MATERIAL);
                render->SetMaterial(fallback);
            }
            else
            {
                render->SetDefaultMaterial();
            }
        }
        else
        {
            shared_ptr<Material> material = load_material(ctx, assimp_material);
            const string spartan_asset_path = ctx.model_directory + material->GetObjectName() + EXTENSION_MATERIAL;
            material->SetResourceFilePath(spartan_asset_path);
            render->SetMaterial(material);
        }
    }

    void ModelImporter::ParseSkeleton(ImportContext& ctx)
    {
        // check if any mesh has bones
        bool has_bones = false;
        for (uint32_t i = 0; i < ctx.scene->mNumMeshes; ++i)
        {
            if (ctx.scene->mMeshes[i]->mNumBones > 0)
            {
                has_bones = true;
                break;
            }
        }

        if (!has_bones)
        {
            return;
        }

        // build the skeleton and populate the bone name map (same indices)
        shared_ptr<Skeleton> skeleton = build_skeleton(ctx.scene, ctx.bone_name_to_index);
        if (!skeleton)
        {
            return;
        }

        ctx.mesh->SetSkeleton(skeleton);
    }

    void ModelImporter::ParseAnimations(ImportContext& ctx)
    {
        if (!ctx.scene->mAnimations || ctx.scene->mNumAnimations == 0)
        {
            return;
        }

        if (ctx.bone_name_to_index.empty())
        {
            return;
        }

        const uint32_t joint_count = static_cast<uint32_t>(ctx.bone_name_to_index.size());

        for (uint32_t i = 0; i < ctx.scene->mNumAnimations; ++i)
        {
            const aiAnimation* anim = ctx.scene->mAnimations[i];
            const string anim_name = anim->mName.length > 0 ? anim->mName.C_Str() : ("animation_" + to_string(i));
            AnimationClip clip = convert_animation(anim, ctx.bone_name_to_index, joint_count, anim_name);

            ctx.mesh->AddAnimationClip(move(clip));
        }

        SP_LOG_INFO("Loaded %u animation clip(s)", ctx.scene->mNumAnimations);
    }
}
