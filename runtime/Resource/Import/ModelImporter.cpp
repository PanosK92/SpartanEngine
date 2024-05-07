/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "ModelImporter.h"
#include "../../Core/ProgressTracker.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Mesh.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../World/Components/Light.h"
SP_WARNINGS_OFF
#include "assimp/scene.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/version.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
SP_WARNINGS_ON
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
using namespace Assimp;
//============================

namespace Spartan
{
    namespace
    {
        string model_file_path;
        string model_name;
        Mesh* mesh               = nullptr;
        bool model_has_animation = false;
        bool model_is_gltf       = false;
        const aiScene* scene     = nullptr;

        Matrix convert_matrix(const aiMatrix4x4& transform)
        {
            return Matrix
            (
                transform.a1, transform.b1, transform.c1, transform.d1,
                transform.a2, transform.b2, transform.c2, transform.d2,
                transform.a3, transform.b3, transform.c3, transform.d3,
                transform.a4, transform.b4, transform.c4, transform.d4
            );
        }

        Color convert_color(const aiColor4D& ai_color)
        {
            return Color(ai_color.r, ai_color.g, ai_color.b, ai_color.a);
        }

        Color convert_color(const aiColor3D& ai_color)
        {
            return Color(ai_color.r, ai_color.g, ai_color.b, 1.0f);
        }

        Vector3 convert_vector3(const aiVector3D& ai_vector)
        {
            return Vector3(ai_vector.x, ai_vector.y, ai_vector.z);
        }

        Vector2 convert_vector2(const aiVector2D& ai_vector)
        {
            return Vector2(ai_vector.x, ai_vector.y);
        }

        Quaternion convert_quaternion(const aiQuaternion& ai_quaternion)
        {
            return Quaternion(ai_quaternion.x, ai_quaternion.y, ai_quaternion.z, ai_quaternion.w);
        }

        void set_entity_transform(const aiNode* node, shared_ptr<Entity> entity)
        {
            // convert to engine matrix
            const Matrix matrix_engine = convert_matrix(node->mTransformation);

            // apply position, rotation and scale
            entity->SetPositionLocal(matrix_engine.GetTranslation());
            entity->SetRotationLocal(matrix_engine.GetRotation());
            entity->SetScaleLocal(matrix_engine.GetScale());
        }

        constexpr void compute_node_count(const aiNode* node, uint32_t* count)
        {
            if (!node)
                return;

            (*count)++;

            // Process children
            for (uint32_t i = 0; i < node->mNumChildren; i++)
            {
                compute_node_count(node->mChildren[i], count);
            }
        }

        // progress reporting interface
        class AssimpProgress : public ProgressHandler
        {
        public:
            AssimpProgress(const string& file_path)
            {
                m_file_path = file_path;
                m_file_name = FileSystem::GetFileNameFromFilePath(file_path);
            }
            ~AssimpProgress() = default;

            bool Update(float percentage) override { return true; }

            void UpdateFileRead(int current_step, int number_of_steps) override
            {
                // Reading from drive file progress is ignored because it's not called in a consistent manner.
                // At least two calls are needed (start, end), but this can be called only once.
            }

            void UpdatePostProcess(int current_step, int number_of_steps) override
            {
                if (current_step == 0)
                {
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone(); // "Loading model from drive..."
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(number_of_steps, "Post-processing model...");
                }
                else
                {
                    ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
                }
            }

        private:
            string m_file_path;
            string m_file_name;
        };

        string texture_try_multiple_extensions(const string& file_path)
        {
            // Remove extension
            const string file_path_no_ext = FileSystem::GetFilePathWithoutExtension(file_path);

            // Check if the file exists using all engine supported extensions
            for (const auto& supported_format : supported_formats_image)
            {
                string new_file_path = file_path_no_ext + supported_format;
                string new_file_path_upper = file_path_no_ext + FileSystem::ConvertToUppercase(supported_format);

                if (FileSystem::Exists(new_file_path))
                {
                    return new_file_path;
                }

                if (FileSystem::Exists(new_file_path_upper))
                {
                    return new_file_path_upper;
                }
            }

            return file_path;
        }

        string texture_validate_path(string original_texture_path, const string& file_path)
        {
            // Models usually return a texture path which is relative to the model's directory.
            // However, to load anything, we'll need an absolute path, so we construct it here.
            const string model_dir = FileSystem::GetDirectoryFromFilePath(file_path);
            string full_texture_path = model_dir + original_texture_path;

            // 1. Check if the texture path is valid
            if (FileSystem::Exists(full_texture_path))
                return full_texture_path;

            // 2. Check the same texture path as previously but 
            // this time with different file extensions (jpg, png and so on).
            full_texture_path = texture_try_multiple_extensions(full_texture_path);
            if (FileSystem::Exists(full_texture_path))
                return full_texture_path;

            // At this point we know the provided path is wrong, we will make a few guesses.
            // The most common mistake is that the artist provided a path which is absolute to his computer.

            // 3. Check if the texture is in the same folder as the model
            full_texture_path = model_dir + FileSystem::GetFileNameFromFilePath(full_texture_path);
            if (FileSystem::Exists(full_texture_path))
                return full_texture_path;

            // 4. Check the same texture path as previously but 
            // this time with different file extensions (jpg, png and so on).
            full_texture_path = texture_try_multiple_extensions(full_texture_path);
            if (FileSystem::Exists(full_texture_path))
                return full_texture_path;

            // Give up, no valid texture path was found
            return "";
        }

        bool load_material_texture(
            Mesh* mesh,
            const string& file_path,
            const bool is_gltf,
            shared_ptr<Material> material,
            const aiMaterial* material_assimp,
            const MaterialTexture texture_type,
            const aiTextureType texture_type_assimp_pbr,
            const aiTextureType texture_type_assimp_legacy
        )
        {
            // determine if this is a pbr material or not
            aiTextureType type_assimp = aiTextureType_NONE;
            type_assimp = material_assimp->GetTextureCount(texture_type_assimp_pbr) > 0 ? texture_type_assimp_pbr : type_assimp;
            type_assimp = (type_assimp == aiTextureType_NONE) ? (material_assimp->GetTextureCount(texture_type_assimp_legacy) > 0 ? texture_type_assimp_legacy : type_assimp) : type_assimp;

            // check if the material has any textures
            if (material_assimp->GetTextureCount(type_assimp) == 0)
                return true;

            // try to get the texture path
            aiString texture_path;
            if (material_assimp->GetTexture(type_assimp, 0, &texture_path) != AI_SUCCESS)
                return false;

            // see if the texture type is supported by the engine
            const string deduced_path = texture_validate_path(texture_path.data, file_path);
            if (!FileSystem::IsSupportedImageFile(deduced_path))
                return false;

            // add the texture to the model
            mesh->AddTexture(material, texture_type, texture_validate_path(texture_path.data, file_path), is_gltf);

            // FIX: materials that have a diffuse texture should not be tinted black/gray
            if (type_assimp == aiTextureType_BASE_COLOR || type_assimp == aiTextureType_DIFFUSE)
            {
                material->SetProperty(MaterialProperty::ColorR, 1.0f);
                material->SetProperty(MaterialProperty::ColorG, 1.0f);
                material->SetProperty(MaterialProperty::ColorB, 1.0f);
                material->SetProperty(MaterialProperty::ColorA, 1.0f);
            }

            // FIX: Some models pass a normal map as a height map and vice versa, we correct that
            if (texture_type == MaterialTexture::Normal || texture_type == MaterialTexture::Height)
            {
                if (shared_ptr<RHI_Texture> texture = material->GetTexture_PtrShared(texture_type))
                {
                    MaterialTexture proper_type = texture_type;
                    proper_type = (proper_type == MaterialTexture::Normal && texture->IsGrayscale()) ? MaterialTexture::Height : proper_type;
                    proper_type = (proper_type == MaterialTexture::Height && !texture->IsGrayscale()) ? MaterialTexture::Normal : proper_type;

                    if (proper_type != texture_type)
                    {
                        material->SetTexture(texture_type, shared_ptr<RHI_Texture>(nullptr));
                        material->SetTexture(proper_type, texture);
                    }
                }
            }

            return true;
        }

        shared_ptr<Material> load_material(Mesh* mesh, const string& file_path, const bool is_gltf, const aiMaterial* material_assimp)
        {
            SP_ASSERT(material_assimp != nullptr);
            shared_ptr<Material> material = make_shared<Material>();

            //                                                                         texture type,                texture type assimp (pbr),       texture type assimp (legacy/fallback)
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Color,      aiTextureType_BASE_COLOR,        aiTextureType_DIFFUSE);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Roughness,  aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS); // use specular as fallback
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Metalness,  aiTextureType_METALNESS,         aiTextureType_NONE);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Normal,     aiTextureType_NORMAL_CAMERA,     aiTextureType_NORMALS);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Occlusion,  aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Emission,   aiTextureType_EMISSION_COLOR,    aiTextureType_EMISSIVE);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::Height,     aiTextureType_HEIGHT,            aiTextureType_NONE);
            load_material_texture(mesh, file_path, is_gltf, material, material_assimp, MaterialTexture::AlphaMask,  aiTextureType_OPACITY,           aiTextureType_NONE);

            // name
            aiString name_assimp;
            aiGetMaterialString(material_assimp, AI_MATKEY_NAME, &name_assimp);
            string name = name_assimp.C_Str();
            // set a material file path, this allows for the material to be cached (also means that if already cached, the engine will not save it as a duplicate)
            material->SetResourceFilePath(FileSystem::RemoveIllegalCharacters(FileSystem::GetDirectoryFromFilePath(file_path) + name + EXTENSION_MATERIAL));

            // color
            aiColor4D color_diffuse(1.0f, 1.0f, 1.0f, 1.0f);
            aiGetMaterialColor(material_assimp, AI_MATKEY_COLOR_DIFFUSE, &color_diffuse);

            // opacity
            aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
            {
                aiGetMaterialColor(material_assimp, AI_MATKEY_OPACITY, &opacity);

                // convert name to lowercase for case insensitive comparisons below
                transform(name.begin(), name.end(), name.begin(), ::tolower);

                // detect transparency
                bool is_transparent = opacity.r < 1.0f;
                if (!is_transparent)
                {
                    is_transparent =
                        name.find("glass")       != string::npos ||
                        name.find("transparent") != string::npos ||
                        name.find("bottle")      != string::npos;
                }

                // set appropriate properties for transparents
                if (is_transparent)
                {
                    opacity.r = 0.5f;
                    material->SetProperty(MaterialProperty::Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::Ior, Material::EnumToIor(MaterialIor::Glass));
                }
            }

            // set color and opacity
            material->SetProperty(MaterialProperty::ColorR, color_diffuse.r);
            material->SetProperty(MaterialProperty::ColorG, color_diffuse.g);
            material->SetProperty(MaterialProperty::ColorB, color_diffuse.b);
            material->SetProperty(MaterialProperty::ColorA, opacity.r);

            // set roughness and metalness mode
            material->SetProperty(MaterialProperty::SingleTextureRoughnessMetalness, static_cast<float>(is_gltf));

            // two-sided
            int no_culling = opacity.r != 1.0f; // if transparent, default to no culling
            aiGetMaterialInteger(material_assimp, AI_MATKEY_TWOSIDED, &no_culling);
            if (no_culling != 0)
            {
                material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
            }

            // if metalness and/or roughness are not provided, try to deduce some sensible values
            {
                bool is_vegetation =
                    name.find("foliage") != string::npos ||
                    name.find("leaf")    != string::npos ||
                    name.find("leaves")  != string::npos ||
                    name.find("flowers") != string::npos ||
                    name.find("plant")   != string::npos;

                bool is_metal =
                    name.find("metal")    != string::npos ||
                    name.find("iron")     != string::npos ||
                    name.find("radiator") != string::npos ||
                    name.find("chrome")   != string::npos;

                bool is_smooth  = name.find("ceramic") != string::npos; // plate
                bool is_plaster = name.find("plaster") != string::npos; // wall
                bool is_tile    = name.find("tile")    != string::npos; // floor

                // metalness
                if (!material->HasTexture(MaterialTexture::Metalness) && is_metal)
                {
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                // roughness
                if (!material->HasTexture(MaterialTexture::Roughness))
                {
                    if (is_smooth || is_metal || is_vegetation)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    }
                    else if (is_tile)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.5f);
                    }
                    else if (is_plaster)
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.65f);
                    }
                }

                // subsurface scattering
                if (is_vegetation)
                {
                    material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                }
            }

            return material;
        }
    }

    void ModelImporter::Initialize()
    {
        const int major = aiGetVersionMajor();
        const int minor = aiGetVersionMinor();
        const int rev   = aiGetVersionRevision();
        Settings::RegisterThirdPartyLib("Assimp", to_string(major) + "." + to_string(minor) + "." + to_string(rev), "https://github.com/assimp/assimp");
    }

    bool ModelImporter::Load(Mesh* mesh_in, const string& file_path)
    {
        SP_ASSERT_MSG(mesh_in != nullptr, "Invalid parameter");

        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("Provided file path doesn't point to an existing file");
            return false;
        }

        // model params
        model_file_path = file_path;
        model_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        mesh            = mesh_in;
        model_is_gltf   = FileSystem::GetExtensionFromFilePath(file_path) == ".gltf";
        mesh->SetObjectName(model_name);

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
            import_flags |= aiProcess_ValidateDataStructure; // Validates the imported scene data structure.
            import_flags |= aiProcess_Triangulate;           // Triangulates all faces of all meshes.
            import_flags |= aiProcess_SortByPType;           // Splits meshes with more than one primitive type in homogeneous sub-meshes.

            // switch to engine conventions
            import_flags |= aiProcess_MakeLeftHanded;   // DirectX style.
            import_flags |= aiProcess_FlipUVs;          // DirectX style.
            import_flags |= aiProcess_FlipWindingOrder; // DirectX style.

            // generate missing normals or UVs
            import_flags |= aiProcess_CalcTangentSpace; // Calculates  tangents and bitangents
            import_flags |= aiProcess_GenSmoothNormals; // Ignored if the mesh already has normals
            import_flags |= aiProcess_GenUVCoords;      // Converts non-UV mappings (such as spherical or cylindrical mapping) to proper texture coordinate channels

            // Combine meshes
            if (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportCombineMeshes))
            {
                import_flags |= aiProcess_OptimizeMeshes;
                import_flags |= aiProcess_OptimizeGraph;
                import_flags |= aiProcess_PreTransformVertices;
            }

            // validate
            if (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportRemoveRedundantData))
            {
                import_flags |= aiProcess_RemoveRedundantMaterials; // Searches for redundant/unreferenced materials and removes them
                import_flags |= aiProcess_JoinIdenticalVertices;    // Identifies and joins identical vertex data sets within all imported meshes
                import_flags |= aiProcess_FindDegenerates;          // Convert degenerate primitives to proper lines or points.
                import_flags |= aiProcess_FindInvalidData;          // This step searches all meshes for invalid data, such as zeroed normal vectors or invalid UV coords and removes / fixes them
                import_flags |= aiProcess_FindInstances;            // This step searches for duplicate meshes and replaces them with references to the first mesh
            }
        }

        ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(1, "Loading model from drive...");

        // read the 3D model file from drive
        if (scene = importer.ReadFile(file_path, import_flags))
        {
            // update progress tracking
            uint32_t job_count = 0;
            compute_node_count(scene->mRootNode, &job_count);
            ProgressTracker::GetProgress(ProgressType::ModelImporter).Start(job_count, "Parsing model...");

            model_has_animation = scene->mNumAnimations != 0;

            // recursively parse nodes
            ParseNode(scene->mRootNode);

            // update model geometry
            {
                while (ProgressTracker::GetProgress(ProgressType::ModelImporter).GetFraction() != 1.0f)
                {
                    SP_LOG_INFO("Waiting for node processing threads to finish before creating GPU buffers...");
                    this_thread::sleep_for(std::chrono::milliseconds(16));
                }

                // optimize
                if ((mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::OptimizeVertexCache)) ||
                    (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::OptimizeVertexFetch)) ||
                    (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::OptimizeOverdraw)))
                {
                    mesh->Optimize();
                }

                // aabb
                mesh->ComputeAabb();

                // normalize scale
                if (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportNormalizeScale))
                {
                    float normalized_scale = mesh->ComputeNormalizedScale();
                    mesh->GetRootEntity().lock()->SetScale(normalized_scale);
                }

                mesh->CreateGpuBuffers();
            }

            // make the root entity active since it's now thread-safe
            mesh->GetRootEntity().lock()->SetActive(true);
            World::Resolve();
        }
        else
        {
            ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
            SP_LOG_ERROR("%s", importer.GetErrorString());
        }

        importer.FreeScene();
        mesh = nullptr;

        return scene != nullptr;
    }

    void ModelImporter::ParseNode(const aiNode* node, shared_ptr<Entity> parent_entity)
    {
        // create an entity that will match this node.
        shared_ptr<Entity> entity = World::CreateEntity();

        // set root entity to mesh
        bool is_root_node = parent_entity == nullptr;
        if (is_root_node)
        {
            mesh->SetRootEntity(entity);

            // the root entity is created as inactive for thread-safety.
            entity->SetActive(false);
        }

        // name the entity
        string node_name = is_root_node ? model_name : node->mName.C_Str();
        entity->SetObjectName(model_name);

        // update progress tracking
        ProgressTracker::GetProgress(ProgressType::ModelImporter).SetText("Creating entity for " + entity->GetObjectName());

        // set the transform of parent_node as the parent of the new_entity's transform
        entity->SetParent(parent_entity);

        // apply node transformation
        set_entity_transform(node, entity);

        // mesh components
        if (node->mNumMeshes > 0)
        {
            ParseNodeMeshes(node, entity);
        }

        // light component
        if (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::ImportLights))
        {
            ParseNodeLight(node, entity);
        }

        // children nodes
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ParseNode(node->mChildren[i], entity);
        }

        // update progress tracking
        ProgressTracker::GetProgress(ProgressType::ModelImporter).JobDone();
    }

    void ModelImporter::ParseNodeMeshes(const aiNode* assimp_node, shared_ptr<Entity> node_entity)
    {
        // An aiNode can have any number of meshes (albeit typically, it's one).
        // If it has more than one meshes, then we create children entities to store them.

        SP_ASSERT_MSG(assimp_node->mNumMeshes != 0, "No meshes to process");

        for (uint32_t i = 0; i < assimp_node->mNumMeshes; i++)
        {
            shared_ptr<Entity> entity = node_entity;
            aiMesh* node_mesh         = scene->mMeshes[assimp_node->mMeshes[i]];
            string node_name          = assimp_node->mName.C_Str();

            // if this node has more than one meshes, create an entity for each mesh, then make that entity a child of node_entity
            if (assimp_node->mNumMeshes > 1)
            {
                // create entity
                entity = World::CreateEntity();

                // set parent
                entity->SetParent(node_entity);

                // set name
                node_name += "_" + to_string(i + 1); // set name
            }

            // set entity name
            entity->SetObjectName(node_name);
            
            // load the mesh onto the entity (via a Renderable component)
            ParseMesh(node_mesh, entity);
        }
    }

    void ModelImporter::ParseNodeLight(const aiNode* node, shared_ptr<Entity> new_entity)
    {
        for (uint32_t i = 0; i < scene->mNumLights; i++)
        {
            if (scene->mLights[i]->mName == node->mName)
            {
                // get assimp light
                const aiLight* light_assimp = scene->mLights[i];

                // add a light component
                shared_ptr<Light> light = new_entity->AddComponent<Light>();

                // disable shadows (to avoid tanking the framerate)
                light->SetFlag(LightFlags::Shadows, false);
                light->SetFlag(LightFlags::ShadowsTransparent, false);
                light->SetFlag(LightFlags::Volumetric, false);

                // local transform
                light->GetEntity()->SetPositionLocal(convert_vector3(light_assimp->mPosition));
                light->GetEntity()->SetRotationLocal(Quaternion::FromLookRotation(convert_vector3(light_assimp->mDirection)));

                // color
                light->SetColor(convert_color(light_assimp->mColorDiffuse));

                // type
                if (light_assimp->mType == aiLightSource_DIRECTIONAL)
                {
                    light->SetLightType(LightType::Directional);
                }
                else if (light_assimp->mType == aiLightSource_POINT)
                {
                    light->SetLightType(LightType::Point);
                }
                else if (light_assimp->mType == aiLightSource_SPOT)
                {
                    light->SetLightType(LightType::Spot);
                }

                // intensity
                light->SetIntensity(LightIntensity::bulb_150_watt);
            }
        }
    }

    void ModelImporter::ParseMesh(aiMesh* assimp_mesh, shared_ptr<Entity> entity_parent)
    {
        SP_ASSERT(assimp_mesh != nullptr);
        SP_ASSERT(entity_parent != nullptr);

        const uint32_t vertex_count = assimp_mesh->mNumVertices;
        const uint32_t index_count  = assimp_mesh->mNumFaces * 3;

        // vertices
        vector<RHI_Vertex_PosTexNorTan> vertices = vector<RHI_Vertex_PosTexNorTan>(vertex_count);
        {
            for (uint32_t i = 0; i < vertex_count; i++)
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
                const uint32_t uv_channel = 0;
                if (assimp_mesh->HasTextureCoords(uv_channel))
                {
                    const auto& tex_coords = assimp_mesh->mTextureCoords[uv_channel][i];
                    vertex.tex[0] = tex_coords.x;
                    vertex.tex[1] = tex_coords.y;
                }
            }
        }

        // indices
        vector<uint32_t> indices = vector<uint32_t>(index_count);
        {
            // get indices by iterating through each face of the mesh.
            for (uint32_t face_index = 0; face_index < assimp_mesh->mNumFaces; face_index++)
            {
                // if (aiPrimitiveType_LINE | aiPrimitiveType_POINT) && aiProcess_Triangulate) then (face.mNumIndices == 3)
                const aiFace& face           = assimp_mesh->mFaces[face_index];
                const uint32_t indices_index = (face_index * 3);
                indices[indices_index + 0]   = face.mIndices[0];
                indices[indices_index + 1]   = face.mIndices[1];
                indices[indices_index + 2]   = face.mIndices[2];
            }
        }

        // compute AABB (before doing move operation on vertices)
        const BoundingBox aabb = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));

        // add vertex and index data to the mesh
        uint32_t index_offset  = 0;
        uint32_t vertex_offset = 0;
        mesh->AddIndices(indices,  &index_offset);
        mesh->AddVertices(vertices, &vertex_offset);

        // add a renderable component to this entity
        shared_ptr<Renderable> renderable = entity_parent->AddComponent<Renderable>();

        // set the geometry
        renderable->SetGeometry(
            mesh,
            aabb,
            index_offset,
            static_cast<uint32_t>(indices.size()),
            vertex_offset,
            static_cast<uint32_t>(vertices.size())
        );

        // material
        if (scene->HasMaterials())
        {
            // get aiMaterial
            const aiMaterial* assimp_material = scene->mMaterials[assimp_mesh->mMaterialIndex];

            // convert it and add it to the model
            shared_ptr<Material> material = load_material(mesh, model_file_path, model_is_gltf, assimp_material);

            mesh->SetMaterial(material, entity_parent.get());
        }

        // Bones
        ParseNodes(assimp_mesh);
    }

    void ModelImporter::ParseAnimations()
    {
        for (uint32_t i = 0; i < scene->mNumAnimations; i++)
        {
            const auto assimp_animation = scene->mAnimations[i];
            auto animation = make_shared<Animation>();

            // Basic properties
            animation->SetObjectName(assimp_animation->mName.C_Str());
            animation->SetDuration(assimp_animation->mDuration);
            animation->SetTicksPerSec(assimp_animation->mTicksPerSecond != 0.0f ? assimp_animation->mTicksPerSecond : 25.0f);

            // Animation channels
            for (uint32_t j = 0; j < static_cast<uint32_t>(assimp_animation->mNumChannels); j++)
            {
                const aiNodeAnim* assimp_node_anim = assimp_animation->mChannels[j];
                AnimationNode animation_node;

                animation_node.name = assimp_node_anim->mNodeName.C_Str();

                // Position keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumPositionKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_vector3(assimp_node_anim->mPositionKeys[k].mValue);

                    animation_node.positionFrames.emplace_back(KeyVector{ time, value });
                }

                // Rotation keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumRotationKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_quaternion(assimp_node_anim->mRotationKeys[k].mValue);

                    animation_node.rotationFrames.emplace_back(KeyQuaternion{ time, value });
                }

                // Scaling keys
                for (uint32_t k = 0; k < static_cast<uint32_t>(assimp_node_anim->mNumScalingKeys); k++)
                {
                    const auto time = assimp_node_anim->mPositionKeys[k].mTime;
                    const auto value = convert_vector3(assimp_node_anim->mScalingKeys[k].mValue);

                    animation_node.scaleFrames.emplace_back(KeyVector{ time, value });
                }
            }
        }
    }

    void ModelImporter::ParseNodes(const aiMesh* assimp_mesh)
    {
        // Maximum number of bones per mesh
        // Must not be higher than same const in skinning shader
        constexpr uint8_t MAX_BONES = 64;
        // Maximum number of bones per vertex
        constexpr uint8_t MAX_BONES_PER_VERTEX = 4;

        //for (uint32_t i = 0; i < assimp_mesh->mNumBones; i++)
        //{
        //    uint32_t index = 0;

        //    assert(assimp_mesh->mNumBones <= MAX_BONES);

        //    string name = assimp_mesh->mBones[i]->mName.data;

        //    if (boneMapping.find(name) == boneMapping.end())
        //    {
        //        // Bone not present, add new one
        //        index = numBones;
        //        numBones++;
        //        BoneInfo bone;
        //        boneInfo.push_back(bone);
        //        boneInfo[index].offset = pMesh->mBones[i]->mOffsetMatrix;
        //        boneMapping[name] = index;
        //    }
        //    else
        //    {
        //        index = boneMapping[name];
        //    }

        //    for (uint32_t j = 0; j < assimp_mesh->mBones[i]->mNumWeights; j++)
        //    {
        //        uint32_t vertexID = vertexOffset + pMesh->mBones[i]->mWeights[j].mVertexId;
        //        Bones[vertexID].add(index, pMesh->mBones[i]->mWeights[j].mWeight);
        //    }
        //}
        //boneTransforms.resize(numBones);
    }
}
