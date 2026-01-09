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
#include "../../Rendering/Animation.h"
#include "../../Geometry/Mesh.h"
#include "../../Rendering/Material.h"
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
        const int major = aiGetVersionMajor();
        const int minor = aiGetVersionMinor();
        const int rev   = aiGetVersionRevision();
        Settings::RegisterThirdPartyLib("Assimp", to_string(major) + "." + to_string(minor) + "." + to_string(rev), "https://github.com/assimp/assimp");
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
            // update progress tracking
            const uint32_t job_count = compute_node_count(ctx.scene->mRootNode);
            ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(job_count, "Parsing model...");

            // recursively parse nodes
            ParseNode(ctx, ctx.scene->mRootNode);

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

        // add vertex and index data to the mesh
        uint32_t sub_mesh_index = 0;
        ctx.mesh->AddGeometry(vertices, indices, true, &sub_mesh_index);

        // set the geometry
        entity_parent->AddComponent<Renderable>()->SetMesh(ctx.mesh, sub_mesh_index);

        // material
        if (ctx.scene->HasMaterials())
        {
            const aiMaterial* assimp_material = ctx.scene->mMaterials[assimp_mesh->mMaterialIndex];
            shared_ptr<Material> material = load_material(ctx, assimp_material);

            // create a file path for this material
            const string spartan_asset_path = ctx.model_directory + material->GetObjectName() + EXTENSION_MATERIAL;
            material->SetResourceFilePath(spartan_asset_path);

            // add a renderable and set the material to it
            entity_parent->AddComponent<Renderable>()->SetMaterial(material);
        }
    }
}
