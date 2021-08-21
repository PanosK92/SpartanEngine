/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =================================
#include "Spartan.h"
#include "ModelImporter.h"
#include "../ProgressTracker.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Rendering/Model.h"
#include "../../Rendering/Animation.h"
#include "../../World/World.h"
#include "../../World/Components/Renderable.h"
#include "../../World/Entity.h"
#include "../../World/Components/Transform.h"
#include "../../RHI/RHI_Vertex.h"
//============================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
using namespace Assimp;
//=============================

namespace Spartan
{
    static Matrix convert_matrix(const aiMatrix4x4& transform)
    {
        return Matrix
        (
            transform.a1, transform.b1, transform.c1, transform.d1,
            transform.a2, transform.b2, transform.c2, transform.d2,
            transform.a3, transform.b3, transform.c3, transform.d3,
            transform.a4, transform.b4, transform.c4, transform.d4
        );
    }

    static void set_entity_transform(const aiNode* node, Entity* entity)
    {
        if (!entity)
            return;

        // Convert to engine matrix
        const Matrix matrix_engine = convert_matrix(node->mTransformation);

        // Apply position, rotation and scale
        entity->GetTransform()->SetPositionLocal(matrix_engine.GetTranslation());
        entity->GetTransform()->SetRotationLocal(matrix_engine.GetRotation());
        entity->GetTransform()->SetScaleLocal(matrix_engine.GetScale());
    }

    constexpr void compute_node_count(const aiNode* node, int* count)
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

    static Vector4 convert_vector4(const aiColor4D& ai_color)
    {
        return Vector4(ai_color.r, ai_color.g, ai_color.b, ai_color.a);
    }

    static Vector3 convert_vector3(const aiVector3D& ai_vector)
    {
        return Vector3(ai_vector.x, ai_vector.y, ai_vector.z);
    }

    static Vector2 convert_vector2(const aiVector2D& ai_vector)
    {
        return Vector2(ai_vector.x, ai_vector.y);
    }

    static Quaternion convert_quaternion(const aiQuaternion& ai_quaternion)
    {
        return Quaternion(ai_quaternion.x, ai_quaternion.y, ai_quaternion.z, ai_quaternion.w);
    }

    // Implement Assimp:Logger
    class AssimpLogger : public Logger
    {
    public:
        bool attachStream(LogStream* pStream, uint32_t severity) override { return true; }
        bool detatchStream(LogStream* pStream, uint32_t severity) override { return true; }

    private:
        void OnDebug(const char* message) override
        {
#ifdef DEBUG
            LOG_INFO(message, LogType::Info);
#endif
        }

        void OnInfo(const char* message) override
        {
            LOG_INFO(message, LogType::Info);
        }

        void OnWarn(const char* message) override
        {
            LOG_WARNING(message, LogType::Warning);
        }

        void OnError(const char* message) override
        {
            LOG_ERROR(message, LogType::Error);
        }
    };

    // Implement Assimp::ProgressHandler
    class AssimpProgress : public ProgressHandler
    {
    public:
        AssimpProgress(const string& file_path)
        {
            m_file_path = file_path;
            m_file_name = FileSystem::GetFileNameFromFilePath(file_path);

            // Start progress tracking
            auto& progress = ProgressTracker::Get();
            progress.Reset(ProgressType::ModelImporter);
            progress.SetIsLoading(ProgressType::ModelImporter, true);
        }

        ~AssimpProgress()
        {
            ProgressTracker::Get().SetIsLoading(ProgressType::ModelImporter, false);
        }

        bool Update(float percentage) override { return true; }

        void UpdateFileRead(int current_step, int number_of_steps) override
        {
            auto& progress = ProgressTracker::Get();
            progress.SetStatus(ProgressType::ModelImporter, "Loading \"" + m_file_name + "\" from disk...");
            progress.SetJobsDone(ProgressType::ModelImporter, current_step);
            progress.SetJobCount(ProgressType::ModelImporter, number_of_steps);
        }

        void UpdatePostProcess(int current_step, int number_of_steps) override
        {
            auto& progress = ProgressTracker::Get();
            progress.SetStatus(ProgressType::ModelImporter, "Post-Processing \"" + m_file_name + "\"");
            progress.SetJobsDone(ProgressType::ModelImporter, current_step);
            progress.SetJobCount(ProgressType::ModelImporter, number_of_steps);
        }

    private:
        string m_file_path;
        string m_file_name;
    };

    static string texture_try_multiple_extensions(const string& file_path)
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

    static string texture_validate_path(string original_texture_path, const string& model_path)
    {
        replace(original_texture_path.begin(), original_texture_path.end(), '\\', '/');

        // Models usually return a texture path which is relative to the model's directory.
        // However, to load anything, we'll need an absolute path, so we construct it here.
        const string model_dir = FileSystem::GetDirectoryFromFilePath(model_path);
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

    static bool load_material_texture(const ModelParams& params, shared_ptr<Material> material, const aiMaterial* material_assimp, const Material_Property texture_type, const aiTextureType texture_type_assimp_pbr, const aiTextureType texture_type_assimp_legacy)
    {
        // Determine if this is a pbr material or not
        aiTextureType type_assimp = aiTextureType_NONE;
        type_assimp = material_assimp->GetTextureCount(texture_type_assimp_pbr) > 0 ? texture_type_assimp_pbr : type_assimp;
        type_assimp = (type_assimp == aiTextureType_NONE) ? (material_assimp->GetTextureCount(texture_type_assimp_legacy) > 0 ? texture_type_assimp_legacy : type_assimp) : type_assimp;

        // Check if the material has any textures
        if (material_assimp->GetTextureCount(type_assimp) == 0)
            return true;

        // Try to get the texture path
        aiString texture_path;
        if (material_assimp->GetTexture(type_assimp, 0, &texture_path) != AI_SUCCESS)
            return false;

        // See if the texture type is supported by the engine
        const string deduced_path = texture_validate_path(texture_path.data, params.file_path);
        if (!FileSystem::IsSupportedImageFile(deduced_path))
            return false;

        // Add the texture to the model
        params.model->AddTexture(material, texture_type, texture_validate_path(texture_path.data, params.file_path));

        // FIX: materials that have a diffuse texture should not be tinted black/gray
        if (type_assimp == aiTextureType_BASE_COLOR || type_assimp == aiTextureType_DIFFUSE)
        {
            material->SetColorAlbedo(Vector4::One);
        }

        // FIX: Some models pass a normal map as a height map and vice versa, we correct that.
        if (texture_type == Material_Normal || texture_type == Material_Height)
        {
            if (shared_ptr<RHI_Texture> texture = material->GetTexture_PtrShared(texture_type))
            {
                Material_Property proper_type = texture_type;
                proper_type = (proper_type == Material_Normal && texture->IsGrayscale()) ? Material_Height : proper_type;
                proper_type = (proper_type == Material_Height && !texture->IsGrayscale()) ? Material_Normal : proper_type;

                if (proper_type != texture_type)
                {
                    material->SetTextureSlot(texture_type, shared_ptr<RHI_Texture>(nullptr));
                    material->SetTextureSlot(proper_type, texture);
                }
            }
        }

        return true;
    }

    static shared_ptr<Material> load_material(Context* context, const aiMaterial* material_assimp, const ModelParams& params)
    {
        SP_ASSERT(material_assimp != nullptr);

        shared_ptr<Material> material = make_shared<Material>(context);

        // NAME
        aiString name;
        aiGetMaterialString(material_assimp, AI_MATKEY_NAME, &name);

        // Set a resource file path so it can be used by the resource cache
        material->SetResourceFilePath(FileSystem::RemoveIllegalCharacters(FileSystem::GetDirectoryFromFilePath(params.file_path) + string(name.C_Str()) + EXTENSION_MATERIAL));

        // COLOR
        aiColor4D color_diffuse(1.0f, 1.0f, 1.0f, 1.0f);
        aiGetMaterialColor(material_assimp, AI_MATKEY_COLOR_DIFFUSE, &color_diffuse);

        // OPACITY
        aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
        aiGetMaterialColor(material_assimp, AI_MATKEY_OPACITY, &opacity);

        // Set color and opacity
        material->SetColorAlbedo(Vector4(color_diffuse.r, color_diffuse.g, color_diffuse.b, opacity.r));

        //                                                       Texture type,       Texture type Assimp (PBR),       Texture type Assimp (Legacy/fallback)
        load_material_texture(params, material, material_assimp, Material_Color,     aiTextureType_BASE_COLOR,        aiTextureType_DIFFUSE);
        load_material_texture(params, material, material_assimp, Material_Roughness, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS); // Use specular as fallback
        load_material_texture(params, material, material_assimp, Material_Metallic,  aiTextureType_METALNESS,         aiTextureType_AMBIENT);   // Use ambient as fallback
        load_material_texture(params, material, material_assimp, Material_Normal,    aiTextureType_NORMAL_CAMERA,     aiTextureType_NORMALS);
        load_material_texture(params, material, material_assimp, Material_Occlusion, aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP);
        load_material_texture(params, material, material_assimp, Material_Emission,  aiTextureType_EMISSION_COLOR,    aiTextureType_EMISSIVE);
        load_material_texture(params, material, material_assimp, Material_Height,    aiTextureType_HEIGHT,            aiTextureType_NONE);
        load_material_texture(params, material, material_assimp, Material_AlphaMask, aiTextureType_OPACITY,           aiTextureType_NONE);

        return material;
    }

    ModelImporter::ModelImporter(Context* context)
    {
        m_context = context;
        m_world   = context->GetSubsystem<World>();

        // Get version
        const int major = aiGetVersionMajor();
        const int minor = aiGetVersionMinor();
        const int rev   = aiGetVersionRevision();
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("Assimp", to_string(major) + "." + to_string(minor) + "." + to_string(rev), "https://github.com/assimp/assimp");
    }

    bool ModelImporter::Load(Model* model, const string& file_path)
    {
        SP_ASSERT(model != nullptr);

        if (!FileSystem::IsFile(file_path))
        {
            LOG_ERROR("Provided file path doesn't point to an existing file");
            return false;
        }

        // Model params
        ModelParams params;
        params.triangle_limit               = 1000000;
        params.vertex_limit                 = 1000000;
        params.max_normal_smoothing_angle   = 80.0f; // Normals exceeding this limit are not smoothed.
        params.max_tangent_smoothing_angle  = 80.0f; // Tangents exceeding this limit are not smoothed. Default is 45, max is 175
        params.file_path                    = file_path;
        params.name                         = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        params.model                        = model;

        // Set up an Assimp importer
        Importer importer;    
        // Set normal smoothing angle
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, params.max_normal_smoothing_angle);
        // Set tangent smoothing angle
        importer.SetPropertyFloat(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, params.max_tangent_smoothing_angle);
        // Maximum number of triangles in a mesh (before splitting)
        importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, params.triangle_limit);
        // Maximum number of vertices in a mesh (before splitting)
        importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, params.vertex_limit);
        // Remove points and lines.
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
        // Remove cameras and lights
        importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS);
        // Enable progress tracking
        importer.SetPropertyBool(AI_CONFIG_GLOB_MEASURE_TIME, true);
        importer.SetProgressHandler(new AssimpProgress(file_path));
        #ifdef DEBUG
        // Enable logging
        DefaultLogger::set(new AssimpLogger());
        #endif
        
        const auto importer_flags =
            aiProcess_MakeLeftHanded |           // directx style.
            aiProcess_FlipUVs |                  // directx style.
            aiProcess_FlipWindingOrder |         // directx style.
            aiProcess_CalcTangentSpace |         
            aiProcess_GenSmoothNormals |         
            aiProcess_JoinIdenticalVertices |    
            aiProcess_OptimizeMeshes |           // reduce the number of meshes
            aiProcess_ImproveCacheLocality |     // re-order triangles for better vertex cache locality.
            aiProcess_RemoveRedundantMaterials | // remove redundant/unreferenced materials.
            aiProcess_LimitBoneWeights |         
            aiProcess_SplitLargeMeshes |         
            aiProcess_Triangulate |              
            aiProcess_GenUVCoords |              
            aiProcess_SortByPType |              // splits meshes with more than one primitive type in homogeneous sub-meshes.
            aiProcess_FindDegenerates |          // convert degenerate primitives to proper lines or points.
            aiProcess_FindInvalidData |
            aiProcess_FindInstances |
            aiProcess_ValidateDataStructure |
            aiProcess_Debone;

        // aiProcess_FixInfacingNormals - is not reliable and fails often.
        // aiProcess_OptimizeGraph      - works but because it merges as nodes as possible, you can't really click and select anything other than the entire thing.

        // Read the 3D model file from disk
        if (const aiScene* scene = importer.ReadFile(file_path, importer_flags))
        {
            // Update progress tracking
            int job_count = 0;
            compute_node_count(scene->mRootNode, &job_count);
            ProgressTracker::Get().SetJobCount(ProgressType::ModelImporter, job_count);

            params.scene            = scene;
            params.has_animation    = scene->mNumAnimations != 0;

            // Create root entity to match Assimp's root node
            const bool is_active = false;
            shared_ptr<Entity> new_entity = m_world->EntityCreate(is_active);
            new_entity->SetName(params.name); // Set custom name, which is more descriptive than "RootNode"
            params.model->SetRootEntity(new_entity);

            // Parse all nodes, starting from the root node and continuing recursively
            ParseNode(scene->mRootNode, params, nullptr, new_entity.get());
            // Parse animations
            ParseAnimations(params);
            // Update model geometry
            model->UpdateGeometry();
        }
        else
        {
            LOG_ERROR("%s", importer.GetErrorString());
        }

        importer.FreeScene();

        return params.scene != nullptr;
    }

    void ModelImporter::ParseNode(const aiNode* assimp_node, const ModelParams& params, Entity* parent_node, Entity* new_entity)
    {
        if (parent_node) // parent node is already set
        {
            new_entity->SetName(assimp_node->mName.C_Str());
        }

        // Update progress tracking
        ProgressTracker::Get().SetStatus(ProgressType::ModelImporter, "Creating entity for " + new_entity->GetObjectName());

        // Set the transform of parent_node as the parent of the new_entity's transform
        Transform* parent_trans = parent_node ? parent_node->GetTransform() : nullptr;
        new_entity->GetTransform()->SetParent(parent_trans);

        // Set the transformation matrix of the Assimp node to the new node
        set_entity_transform(assimp_node, new_entity);

        // Process all the node's meshes
        ParseNodeMeshes(assimp_node, new_entity, params);

        // Process children
        for (uint32_t i = 0; i < assimp_node->mNumChildren; i++)
        {
            auto child = m_world->EntityCreate();
            ParseNode(assimp_node->mChildren[i], params, new_entity, child.get());
        }

        // Update progress tracking
        ProgressTracker::Get().IncrementJobsDone(ProgressType::ModelImporter);
    }

    void ModelImporter::ParseNodeMeshes(const aiNode* assimp_node, Entity* new_entity, const ModelParams& params)
    {
        for (uint32_t i = 0; i < assimp_node->mNumMeshes; i++)
        {
            auto entity = new_entity; // set the current entity
            const auto assimp_mesh = params.scene->mMeshes[assimp_node->mMeshes[i]]; // get mesh
            string _name = assimp_node->mName.C_Str(); // get name

            // if this node has many meshes, then assign a new entity for each one of them
            if (assimp_node->mNumMeshes > 1)
            {
                const bool is_active = false;
                entity = m_world->EntityCreate(is_active).get(); // create
                entity->GetTransform()->SetParent(new_entity->GetTransform()); // set parent
                _name += "_" + to_string(i + 1); // set name
            }

            // Set entity name
            entity->SetName(_name);

            // Process mesh
            LoadMesh(assimp_mesh, entity, params);
            entity->SetActive(true);
        }
    }

    void ModelImporter::ParseAnimations(const ModelParams& params)
    {
        for (uint32_t i = 0; i < params.scene->mNumAnimations; i++)
        {
            const auto assimp_animation = params.scene->mAnimations[i];
            auto animation = make_shared<Animation>(m_context);

            // Basic properties
            animation->SetName(assimp_animation->mName.C_Str());
            animation->SetDuration(assimp_animation->mDuration);
            animation->SetTicksPerSec(assimp_animation->mTicksPerSecond != 0.0f ? assimp_animation->mTicksPerSecond : 25.0f);

            // Animation channels
            for (uint32_t j = 0; j < static_cast<uint32_t>(assimp_animation->mNumChannels); j++)
            {
                const auto assimp_node_anim = assimp_animation->mChannels[j];
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

    void ModelImporter::LoadMesh(aiMesh* assimp_mesh, Entity* entity_parent, const ModelParams& params)
    {
        SP_ASSERT(assimp_mesh != nullptr);
        SP_ASSERT(entity_parent != nullptr);

        const uint32_t vertex_count = assimp_mesh->mNumVertices;
        const uint32_t index_count  = assimp_mesh->mNumFaces * 3;

        // Vertices
        vector<RHI_Vertex_PosTexNorTan> vertices = vector<RHI_Vertex_PosTexNorTan>(vertex_count);
        {
            for (uint32_t i = 0; i < vertex_count; i++)
            {
                RHI_Vertex_PosTexNorTan& vertex = vertices[i];

                // Position
                const aiVector3D& pos = assimp_mesh->mVertices[i];
                vertex.pos[0] = pos.x;
                vertex.pos[1] = pos.y;
                vertex.pos[2] = pos.z;

                // Normal
                if (assimp_mesh->mNormals)
                {
                    const aiVector3D& normal = assimp_mesh->mNormals[i];
                    vertex.nor[0] = normal.x;
                    vertex.nor[1] = normal.y;
                    vertex.nor[2] = normal.z;
                }

                // Tangent
                if (assimp_mesh->mTangents)
                {
                    const aiVector3D& tangent = assimp_mesh->mTangents[i];
                    vertex.tan[0] = tangent.x;
                    vertex.tan[1] = tangent.y;
                    vertex.tan[2] = tangent.z;
                }

                // Texture coordinates
                const uint32_t uv_channel = 0;
                if (assimp_mesh->HasTextureCoords(uv_channel))
                {
                    const auto& tex_coords = assimp_mesh->mTextureCoords[uv_channel][i];
                    vertex.tex[0] = tex_coords.x;
                    vertex.tex[1] = tex_coords.y;
                }
            }
        }

        // Indices
        vector<uint32_t> indices = vector<uint32_t>(index_count);
        {
            // Get indices by iterating through each face of the mesh.
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

        // Compute AABB (before doing move operation on vertices)
        const BoundingBox aabb = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));

        // Add the mesh to the model
        uint32_t index_offset;
        uint32_t vertex_offset;
        params.model->AppendGeometry(move(indices), move(vertices), &index_offset, &vertex_offset);

        // Add a renderable component to this entity
        Renderable* renderable = entity_parent->AddComponent<Renderable>();

        // Set the geometry
        renderable->GeometrySet(
            entity_parent->GetObjectName(),
            index_offset,
            static_cast<uint32_t>(indices.size()),
            vertex_offset,
            static_cast<uint32_t>(vertices.size()),
            aabb,
            params.model
        );

        // Material
        if (params.scene->HasMaterials())
        {
            // Get aiMaterial
            const aiMaterial* assimp_material = params.scene->mMaterials[assimp_mesh->mMaterialIndex];
            // Convert it and add it to the model
            shared_ptr<Material> material = load_material(m_context, assimp_material, params);
            params.model->AddMaterial(material, entity_parent->GetPtrShared());
        }

        // Bones
        LoadBones(assimp_mesh, params);
    }

    void ModelImporter::LoadBones(const aiMesh* assimp_mesh, const ModelParams& params)
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
