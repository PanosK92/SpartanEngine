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
#include "ModelImporter.h"
#include "../../Core/ProgressTracker.h"
#include "../../Core/ThreadPool.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Geometry/Mesh.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Animation/Skeleton.h"
#include "../../Rendering/Animation/AnimationClip.h"
#include "../../Rendering/Animation/SkeletalMeshBinding.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../../World/Components/Light.h"
#include "../../Resource/ResourceCache.h"
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
    struct ImportContext
    {
        string file_path;
        string model_name;
        string model_directory;
        Mesh* mesh           = nullptr;
        const aiScene* scene = nullptr;
        unordered_map<string, uint32_t> bone_name_to_index;
    };

    namespace
    {
        mutex mutex_import;

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
                return 0;

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

        string resolve_texture_path(const string& original_path, const string& model_directory)
        {
            // try the original path first (relative to model)
            string full_path = model_directory + original_path;
            if (FileSystem::Exists(full_path))
                return full_path;

            // get base path without extension
            const string base_path = FileSystem::GetFilePathWithoutExtension(full_path);
            const string file_name = FileSystem::GetFileNameFromFilePath(original_path);
            const string file_name_no_ext = FileSystem::GetFileNameWithoutExtensionFromFilePath(original_path);

            // common texture formats ordered by likelihood
            static const array<const char*, 8> extensions = {
                ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".PNG", ".JPG"
            };

            // try with different extensions
            for (const char* ext : extensions)
            {
                string test_path = base_path + ext;
                if (FileSystem::Exists(test_path))
                    return test_path;
            }

            // try in model directory (common for absolute paths baked by artists)
            for (const char* ext : extensions)
            {
                string test_path = model_directory + file_name_no_ext + ext;
                if (FileSystem::Exists(test_path))
                    return test_path;
            }

            return "";
        }

        // load texture synchronously (original working behavior)
        bool load_material_texture(
            const string& model_directory,
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
                return true;

            // get texture path
            aiString texture_path;
            if (material_assimp->GetTexture(type_assimp, 0, &texture_path) != AI_SUCCESS)
                return false;

            // resolve actual file path
            const string resolved_path = resolve_texture_path(texture_path.data, model_directory);
            if (!FileSystem::IsSupportedImageFile(resolved_path))
                return false;

            // load the texture and set it to the material
            {
                const string tex_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(resolved_path);
                shared_ptr<RHI_Texture> texture = ResourceCache::GetByName<RHI_Texture>(tex_name);

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

        shared_ptr<Material> load_material(ImportContext& ctx, const aiMaterial* material_assimp)
        {
            SP_ASSERT(material_assimp != nullptr);
            shared_ptr<Material> material = make_shared<Material>();

            // synchronous texture loading (async was causing race conditions with texture packing)
            // note: gltf uses aiTextureType_GLTF_METALLIC_ROUGHNESS for combined metallic-roughness texture
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Color,     aiTextureType_BASE_COLOR,              aiTextureType_DIFFUSE);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Roughness, aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Metalness, aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_METALNESS);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Normal,    aiTextureType_NORMAL_CAMERA,           aiTextureType_NORMALS);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Occlusion, aiTextureType_AMBIENT_OCCLUSION,       aiTextureType_LIGHTMAP);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Emission,  aiTextureType_EMISSION_COLOR,          aiTextureType_EMISSIVE);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::Height,    aiTextureType_HEIGHT,                  aiTextureType_NONE);
            load_material_texture(ctx.model_directory, material, material_assimp, MaterialTextureType::AlphaMask, aiTextureType_OPACITY,                 aiTextureType_NONE);

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

            // two-sided
            int no_culling = opacity.r != 1.0f;
            aiGetMaterialInteger(material_assimp, AI_MATKEY_TWOSIDED, &no_culling);
            if (no_culling != 0)
            {
                material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
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

                        // position
                        const aiVector3D& pos = assimp_mesh->mVertices[i];
                        vertex.pos[0] = pos.x;
                        vertex.pos[1] = pos.y;
                        vertex.pos[2] = pos.z;

                        // normal
                        if (assimp_mesh->mNormals)
                        {
                            const aiVector3D& normal = assimp_mesh->mNormals[i];
                            vertex.nor[0] = normal.x;
                            vertex.nor[1] = normal.y;
                            vertex.nor[2] = normal.z;
                        }

                        // tangent
                        if (assimp_mesh->mTangents)
                        {
                            const aiVector3D& tangent = assimp_mesh->mTangents[i];
                            vertex.tan[0] = tangent.x;
                            vertex.tan[1] = tangent.y;
                            vertex.tan[2] = tangent.z;
                        }

                        // texture coordinates
                        if (assimp_mesh->HasTextureCoords(0))
                        {
                            const auto& tex_coords = assimp_mesh->mTextureCoords[0][i];
                            vertex.tex[0] = tex_coords.x;
                            vertex.tex[1] = tex_coords.y;
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
                        const aiVector3D& normal = assimp_mesh->mNormals[i];
                        vertex.nor[0] = normal.x;
                        vertex.nor[1] = normal.y;
                        vertex.nor[2] = normal.z;
                    }

                    if (assimp_mesh->mTangents)
                    {
                        const aiVector3D& tangent = assimp_mesh->mTangents[i];
                        vertex.tan[0] = tangent.x;
                        vertex.tan[1] = tangent.y;
                        vertex.tan[2] = tangent.z;
                    }

                    if (assimp_mesh->HasTextureCoords(0))
                    {
                        const auto& tex_coords = assimp_mesh->mTextureCoords[0][i];
                        vertex.tex[0] = tex_coords.x;
                        vertex.tex[1] = tex_coords.y;
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

        // find a node by name in the scene hierarchy
        const aiNode* find_node(const aiNode* root, const string& name)
        {
            if (!root)
                return nullptr;

            if (string(root->mName.C_Str()) == name)
                return root;

            for (uint32_t i = 0; i < root->mNumChildren; ++i)
            {
                const aiNode* found = find_node(root->mChildren[i], name);
                if (found)
                    return found;
            }

            return nullptr;
        }

        // resolve parent index for each bone by walking the aiNode hierarchy
        int16_t find_parent_bone_index(const aiNode* bone_node, const unordered_map<string, uint32_t>& name_to_index)
        {
            const aiNode* parent = bone_node->mParent;
            while (parent)
            {
                auto it = name_to_index.find(parent->mName.C_Str());
                if (it != name_to_index.end())
                    return static_cast<int16_t>(it->second);

                parent = parent->mParent;
            }

            return -1;
        }

        // build a skeleton from the scene's bone data
        shared_ptr<Skeleton> build_skeleton(const aiScene* scene)
        {
            vector<string> bone_names;
            unordered_map<string, uint32_t> name_to_index;
            collect_bone_names(scene, bone_names, name_to_index);

            if (bone_names.empty())
                return nullptr;

            auto skeleton = make_shared<Skeleton>();
            const uint16_t joint_count = static_cast<uint16_t>(bone_names.size());
            skeleton->Allocate(joint_count);

            // resolve parent indices and extract bind pose from inverse bind matrices
            vector<Matrix> inverse_bind_matrices(joint_count, Matrix::Identity);

            // gather inverse bind matrices from the first mesh that references each bone
            for (uint32_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
            {
                const aiMesh* mesh = scene->mMeshes[mesh_idx];
                for (uint32_t bone_idx = 0; bone_idx < mesh->mNumBones; ++bone_idx)
                {
                    const aiBone* bone = mesh->mBones[bone_idx];
                    auto it = name_to_index.find(bone->mName.C_Str());
                    if (it != name_to_index.end())
                    {
                        inverse_bind_matrices[it->second] = to_matrix(bone->mOffsetMatrix);
                    }
                }
            }

            // resolve parent hierarchy and extract bind pose
            for (uint32_t i = 0; i < joint_count; ++i)
            {
                const aiNode* bone_node = find_node(scene->mRootNode, bone_names[i]);

                // parent index
                skeleton->m_mutable_parents[i] = bone_node ? find_parent_bone_index(bone_node, name_to_index) : -1;

                // extract local bind pose from the node's local transform
                if (bone_node)
                {
                    const Matrix local_transform = to_matrix(bone_node->mTransformation);
                    skeleton->m_mutable_positions[i] = local_transform.GetTranslation();
                    skeleton->m_mutable_rotations[i] = local_transform.GetRotation();
                    skeleton->m_mutable_scales[i]    = local_transform.GetScale();
                }
                else
                {
                    skeleton->m_mutable_positions[i] = Vector3::Zero;
                    skeleton->m_mutable_rotations[i] = Quaternion::Identity;
                    skeleton->m_mutable_scales[i]    = Vector3::One;
                }
            }

            // ensure root bone has parent -1 (reorder if needed)
            if (skeleton->m_mutable_parents[0] != -1)
            {
                for (uint32_t i = 0; i < joint_count; ++i)
                {
                    if (skeleton->m_mutable_parents[i] == -1)
                    {
                        // swap bone 0 and bone i
                        swap(skeleton->m_mutable_parents[0], skeleton->m_mutable_parents[i]);
                        swap(skeleton->m_mutable_positions[0], skeleton->m_mutable_positions[i]);
                        swap(skeleton->m_mutable_rotations[0], skeleton->m_mutable_rotations[i]);
                        swap(skeleton->m_mutable_scales[0], skeleton->m_mutable_scales[i]);

                        // fix parent references
                        for (uint32_t j = 0; j < joint_count; ++j)
                        {
                            if (skeleton->m_mutable_parents[j] == 0)
                                skeleton->m_mutable_parents[j] = static_cast<int16_t>(i);
                            else if (skeleton->m_mutable_parents[j] == static_cast<int16_t>(i))
                                skeleton->m_mutable_parents[j] = 0;
                        }
                        break;
                    }
                }
            }

            SP_LOG_INFO("Skeleton built with %u joints", joint_count);
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
                return;

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
                    continue;

                const uint16_t global_bone_index = static_cast<uint16_t>(it->second);

                for (uint32_t weight_idx = 0; weight_idx < bone->mNumWeights; ++weight_idx)
                {
                    const aiVertexWeight& vw = bone->mWeights[weight_idx];
                    const uint32_t vertex_id = vw.mVertexId;

                    if (vertex_id >= assimp_mesh->mNumVertices)
                        continue;

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
                    total += influence.bone_weights[w];

                if (total > 0.0f)
                {
                    const float inv = 1.0f / total;
                    for (uint32_t w = 0; w < 4; ++w)
                        influence.bone_weights[w] *= inv;
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

        // convert an assimp animation to the engine's AnimationClip format
        AnimationClip convert_animation(
            const aiAnimation* anim,
            const unordered_map<string, uint32_t>& bone_name_to_index,
            const uint32_t joint_count)
        {
            AnimationClip clip;

            const double ticks_per_second = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;
            clip.duration_seconds = static_cast<float>(anim->mDuration / ticks_per_second);
            clip.sample_rate      = static_cast<float>(ticks_per_second);
            clip.joint_count      = joint_count;

            // initialize base pose to identity
            clip.base_local_positions.resize(joint_count, Vector3::Zero);
            clip.base_local_rotations.resize(joint_count, Quaternion::Identity);
            clip.base_local_scales.resize(joint_count, Vector3::One);

            for (uint32_t ch = 0; ch < anim->mNumChannels; ++ch)
            {
                const aiNodeAnim* channel = anim->mChannels[ch];
                auto it = bone_name_to_index.find(channel->mNodeName.C_Str());
                if (it == bone_name_to_index.end())
                    continue;

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
                    ac.sample_count = channel->mNumPositionKeys;
                    clip.position_stream.channels.push_back(ac);

                    for (uint32_t k = 0; k < channel->mNumPositionKeys; ++k)
                        clip.position_stream.values.push_back(to_vector3(channel->mPositionKeys[k].mValue));

                    clip.base_local_positions[bone_index] = to_vector3(channel->mPositionKeys[0].mValue);
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
                    ac.sample_count = channel->mNumRotationKeys;
                    clip.rotation_stream.channels.push_back(ac);

                    for (uint32_t k = 0; k < channel->mNumRotationKeys; ++k)
                        clip.rotation_stream.values.push_back(to_quaternion(channel->mRotationKeys[k].mValue));

                    clip.base_local_rotations[bone_index] = to_quaternion(channel->mRotationKeys[0].mValue);
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
                    ac.sample_count = channel->mNumScalingKeys;
                    clip.scale_stream.channels.push_back(ac);

                    for (uint32_t k = 0; k < channel->mNumScalingKeys; ++k)
                        clip.scale_stream.values.push_back(to_vector3(channel->mScalingKeys[k].mValue));

                    clip.base_local_scales[bone_index] = to_vector3(channel->mScalingKeys[0].mValue);
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

    void ModelImporter::Initialize()
    {
    
    }

    void ModelImporter::Load(Mesh* mesh_in, const string& file_path)
    {
        SP_ASSERT_MSG(mesh_in != nullptr, "Invalid parameter");

        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("Provided file path doesn't point to an existing file");
            return;
        }

        lock_guard<mutex> guard(mutex_import);

        // initialize import context
        ImportContext ctx;
        ctx.file_path       = file_path;
        ctx.model_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        ctx.model_directory = FileSystem::GetDirectoryFromFilePath(file_path);
        ctx.mesh            = mesh_in;
        ctx.mesh->SetObjectName(ctx.model_name);

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
            import_flags |= aiProcess_GenSmoothNormals;
            import_flags |= aiProcess_GenUVCoords;

            // limit bone weights to 4 per vertex
            import_flags |= aiProcess_LimitBoneWeights;

            // combine meshes
            if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportCombineMeshes))
            {
                import_flags |= aiProcess_OptimizeMeshes;
                import_flags |= aiProcess_PreTransformVertices;
            }

            // validate
            if (ctx.mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportRemoveRedundantData))
            {
                import_flags |= aiProcess_RemoveRedundantMaterials;
                import_flags |= aiProcess_JoinIdenticalVertices;
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

            // recursively parse nodes
            ParseNode(ctx, ctx.scene->mRootNode);

            // extract animation clips
            ParseAnimations(ctx);

            // update model geometry
            {
                while (ProgressTracker::GetProgress(ProgressType::ModelImporter).GetFraction() != 1.0f)
                {
                    SP_LOG_INFO("Waiting for node processing threads to finish before creating GPU buffers...");
                    this_thread::sleep_for(chrono::milliseconds(16));
                }

                ctx.mesh->CreateGpuBuffers();
            }

            // make the root entity active since it's now thread-safe
            ctx.mesh->GetRootEntity()->SetActive(true);
        }
        else
        {
            ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
            SP_LOG_ERROR("%s", importer.GetErrorString());
        }

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
            ParseMesh(ctx, node_mesh, entity);
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

                // local transform
                light->GetEntity()->SetPositionLocal(to_vector3(light_assimp->mPosition));
                light->GetEntity()->SetRotationLocal(Quaternion::FromLookRotation(to_vector3(light_assimp->mDirection)));

                // color
                light->SetColor(to_color(light_assimp->mColorDiffuse));

                // type
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
                        break;
                }
            }
        }
    }

    void ModelImporter::ParseMesh(ImportContext& ctx, aiMesh* assimp_mesh, Entity* entity_parent)
    {
        SP_ASSERT(assimp_mesh != nullptr);
        SP_ASSERT(entity_parent != nullptr);

        // process vertices and indices (parallel for large meshes)
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        process_vertices_parallel(assimp_mesh, vertices);
        process_indices_parallel(assimp_mesh, indices);

        const uint32_t vertex_offset = ctx.mesh->GetVertexCount();

        // add vertex and index data to the mesh
        uint32_t sub_mesh_index = 0;
        ctx.mesh->AddGeometry(vertices, indices, true, &sub_mesh_index);

        // set the geometry
        entity_parent->AddComponent<Render>()->SetMesh(ctx.mesh, sub_mesh_index);

        // extract bone weights if the mesh has bones
        if (assimp_mesh->mNumBones > 0 && !ctx.bone_name_to_index.empty())
        {
            if (!ctx.mesh->GetSkeletalMeshBinding())
                ctx.mesh->SetSkeletalMeshBinding(make_unique<SkeletalMeshBinding>());

            extract_bone_weights(assimp_mesh, ctx.bone_name_to_index, sub_mesh_index, vertex_offset, *ctx.mesh->GetSkeletalMeshBinding());
        }

        // material
        if (ctx.scene->HasMaterials())
        {
            const aiMaterial* assimp_material = ctx.scene->mMaterials[assimp_mesh->mMaterialIndex];
            shared_ptr<Material> material = load_material(ctx, assimp_material);

            // create a file path for this material
            const string spartan_asset_path = ctx.model_directory + material->GetObjectName() + EXTENSION_MATERIAL;
            material->SetResourceFilePath(spartan_asset_path);

            // add a renderable and set the material to it
            entity_parent->AddComponent<Render>()->SetMaterial(material);
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
            return;

        // build the skeleton and populate the bone name map
        shared_ptr<Skeleton> skeleton = build_skeleton(ctx.scene);
        if (!skeleton)
            return;

        ctx.mesh->SetSkeleton(skeleton);

        // populate the context bone name map for use during mesh parsing
        vector<string> bone_names;
        collect_bone_names(ctx.scene, bone_names, ctx.bone_name_to_index);
    }

    void ModelImporter::ParseAnimations(ImportContext& ctx)
    {
        if (!ctx.scene->mAnimations || ctx.scene->mNumAnimations == 0)
            return;

        if (ctx.bone_name_to_index.empty())
            return;

        const uint32_t joint_count = static_cast<uint32_t>(ctx.bone_name_to_index.size());

        for (uint32_t i = 0; i < ctx.scene->mNumAnimations; ++i)
        {
            const aiAnimation* anim = ctx.scene->mAnimations[i];
            AnimationClip clip = convert_animation(anim, ctx.bone_name_to_index, joint_count);

            const string anim_name = anim->mName.length > 0 ? anim->mName.C_Str() : ("animation_" + to_string(i));
            SP_LOG_INFO("Animation clip '%s' loaded: %.2fs, %u joints, %u channels",
                anim_name.c_str(), clip.duration_seconds, clip.joint_count,
                static_cast<uint32_t>(clip.position_stream.channels.size()));

            ctx.mesh->AddAnimationClip(move(clip));
        }

        SP_LOG_INFO("Loaded %u animation clip(s)", ctx.scene->mNumAnimations);
    }
}
