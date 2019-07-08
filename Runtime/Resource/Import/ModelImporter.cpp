/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "ModelImporter.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/version.h>
#include "AssimpHelper.h"
#include "../ProgressReport.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Model.h"
#include "../../Rendering/Animation.h"
#include "../../Rendering/Material.h"
#include "../../World/World.h"
#include "../../World/Components/Renderable.h"
//============================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
using namespace Assimp;
//=============================

namespace Spartan
{
	namespace _ModelImporter
	{
		static float max_normal_smoothing_angle		= 80.0f;	// Normals exceeding this limit are not smoothed.
		static float max_tangent_smoothing_angle	= 80.0f;	// Tangents exceeding this limit are not smoothed. Default is 45, max is 175
		std::string m_model_path;

		// Things for Assimp to do
		static auto flags =
			aiProcess_CalcTangentSpace |
			aiProcess_GenSmoothNormals |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes |
			aiProcess_ImproveCacheLocality |
			aiProcess_LimitBoneWeights |
			aiProcess_SplitLargeMeshes |
			aiProcess_Triangulate |
			aiProcess_GenUVCoords |
			aiProcess_SortByPType |
			aiProcess_FindDegenerates |
			aiProcess_FindInvalidData |
			aiProcess_FindInstances |
			aiProcess_ValidateDataStructure |
			aiProcess_Debone |
			aiProcess_ConvertToLeftHanded;
	}

	ModelImporter::ModelImporter(Context* context)
	{
		m_context	= context;
		m_world		= context->GetSubsystem<World>().get();

		// Get version
		const int major	= aiGetVersionMajor();
		const int minor	= aiGetVersionMinor();
		const int rev	= aiGetVersionRevision();
        context->GetSubsystem<Settings>()->m_versionAssimp = to_string(major) + "." + to_string(minor) + "." + to_string(rev);
	}

	bool ModelImporter::Load(Model* model, const string& file_path)
	{
		if (!m_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_ModelImporter::m_model_path = file_path;

		// Set up an Assimp importer
		Importer importer;	
		// Set normal smoothing angle
		importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, _ModelImporter::max_normal_smoothing_angle);
		// Set tangent smoothing angle
		importer.SetPropertyFloat(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, _ModelImporter::max_tangent_smoothing_angle);	
		// Maximum number of triangles in a mesh (before splitting)
		const uint32_t triangle_limit = 1000000;
		importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, triangle_limit);
		// Maximum number of vertices in a mesh (before splitting)
		const uint32_t vertex_limit = 1000000;
		importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, vertex_limit);
		// Remove points and lines.
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);	
		// Remove cameras and lights
		importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS);		
		// Enable progress tracking
		importer.SetPropertyBool(AI_CONFIG_GLOB_MEASURE_TIME, true);
		importer.SetProgressHandler(new AssimpHelper::AssimpProgress(file_path));
		// Enable logging
		DefaultLogger::set(new AssimpHelper::AssimpLogger());

		// Read the 3D model file from disk
		const auto scene = importer.ReadFile(_ModelImporter::m_model_path, _ModelImporter::flags);
		const auto result = scene != nullptr;
		if (result)
		{
			FIRE_EVENT(Event_World_Stop);
			ReadNodeHierarchy(scene, scene->mRootNode, model);
			ReadAnimations(scene, model);
			model->GeometryUpdate();
			FIRE_EVENT(Event_World_Start);
		}
		else
		{
			LOGF_ERROR("%s", importer.GetErrorString());
		}

		importer.FreeScene();

		return result;
	}

	void ModelImporter::ReadNodeHierarchy(const aiScene* assimp_scene, aiNode* assimp_node, Model* model, Entity* parent_node, Entity* new_entity)
	{
		// Is this the root node?
		if (!assimp_node->mParent || !new_entity)
		{
			new_entity = m_world->EntityCreate().get();
			model->SetRootentity(new_entity->GetPtrShared());

			int job_count;
			AssimpHelper::compute_node_count(assimp_node, &job_count);
			ProgressReport::Get().SetJobCount(g_progress_model_importer, job_count);
		}

		//= GET NODE NAME ==========================================================================================================================
		// In case this is the root node, aiNode.mName will be "RootNode". 
		// To get a more descriptive name we instead get the name from the file path.
		const auto name = assimp_node->mParent ? assimp_node->mName.C_Str() : FileSystem::GetFileNameNoExtensionFromFilePath(_ModelImporter::m_model_path);
		new_entity->SetName(name);
		ProgressReport::Get().SetStatus(g_progress_model_importer, "Creating entity for " + name);
		//==========================================================================================================================================

		// Set the transform of parentNode as the parent of the newNode's transform
		const auto parent_trans = parent_node ? parent_node->GetTransform_PtrRaw() : nullptr;
		new_entity->GetTransform_PtrRaw()->SetParent(parent_trans);

		// Set the transformation matrix of the Assimp node to the new node
		AssimpHelper::set_entity_transform(assimp_node, new_entity);

		// Process all the node's meshes
		for (uint32_t i = 0; i < assimp_node->mNumMeshes; i++)
		{
			auto entity				= new_entity; // set the current entity
			const auto assimp_mesh	= assimp_scene->mMeshes[assimp_node->mMeshes[i]]; // get mesh
			string _name			= assimp_node->mName.C_Str(); // get name

			// if this node has many meshes, then assign a new entity for each one of them
			if (assimp_node->mNumMeshes > 1)
			{
				entity = m_world->EntityCreate().get(); // create
				entity->GetTransform_PtrRaw()->SetParent(new_entity->GetTransform_PtrRaw()); // set parent
				_name += "_" + to_string(i + 1); // set name
			}

			// Set entity name
			entity->SetName(_name);

			// Process mesh
			LoadMesh(assimp_scene, assimp_mesh, model, entity);
		}

		// Process children
		for (uint32_t i = 0; i < assimp_node->mNumChildren; i++)
		{
			auto child = m_world->EntityCreate();
			ReadNodeHierarchy(assimp_scene, assimp_node->mChildren[i], model, new_entity, child.get());
		}

		ProgressReport::Get().IncrementJobsDone(g_progress_model_importer);
	}

	void ModelImporter::ReadAnimations(const aiScene* scene, Model* model)
	{
		for (uint32_t i = 0; i < scene->mNumAnimations; i++)
		{
			const auto assimp_animation = scene->mAnimations[i];
			auto animation = make_shared<Animation>(m_context);

			// Basic properties
			animation->SetName(assimp_animation->mName.C_Str());
			animation->SetDuration(assimp_animation->mDuration);
			animation->SetTicksPerSec(assimp_animation->mTicksPerSecond != 0.0f ? assimp_animation->mTicksPerSecond : 25.0f);

			// Animation channels
			for (uint32_t j = 0; j > assimp_animation->mNumChannels; j++)
			{
				const auto assimp_node_anim = assimp_animation->mChannels[j];
				AnimationNode animation_node;

				animation_node.name = assimp_node_anim->mNodeName.C_Str();

				// Position keys
				for (uint32_t k = 0; k < assimp_node_anim->mNumPositionKeys; k++)
				{
					const auto time = assimp_node_anim->mPositionKeys[k].mTime;
					const auto value = AssimpHelper::to_vector3(assimp_node_anim->mPositionKeys[k].mValue);

					animation_node.positionFrames.emplace_back(KeyVector{ time, value });
				}

				// Rotation keys
				for (uint32_t k = 0; k < assimp_node_anim->mNumRotationKeys; k++)
				{
					const auto time = assimp_node_anim->mPositionKeys[k].mTime;
					const auto value = AssimpHelper::to_quaternion(assimp_node_anim->mRotationKeys[k].mValue);

					animation_node.rotationFrames.emplace_back(KeyQuaternion{ time, value });
				}

				// Scaling keys
				for (uint32_t k = 0; k < assimp_node_anim->mNumScalingKeys; k++)
				{
					const auto time = assimp_node_anim->mPositionKeys[k].mTime;
					const auto value = AssimpHelper::to_vector3(assimp_node_anim->mScalingKeys[k].mValue);

					animation_node.scaleFrames.emplace_back(KeyVector{ time, value });
				}
			}

			model->AddAnimation(animation);
		}
	}

	void ModelImporter::LoadMesh(const aiScene* assimp_scene, aiMesh* assimp_mesh, Model* model, Entity* entity_parent)
	{
		if (!model || !assimp_mesh || !assimp_scene || !entity_parent)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Vertices
		vector<RHI_Vertex_PosTexNorTan> vertices;
		{
			// Pre-allocate for extra performance
			const auto vertex_count = assimp_mesh->mNumVertices;
			vertices.reserve(vertex_count);
			vertices.resize(vertex_count);

			for (uint32_t i = 0; i < vertex_count; i++)
			{
				auto& vertex = vertices[i];

				// Position
				const auto& pos = assimp_mesh->mVertices[i];
				vertex.pos[0] = pos.x;
				vertex.pos[1] = pos.y;
				vertex.pos[2] = pos.z;

				// Normal
				if (assimp_mesh->mNormals)
				{
					const auto& normal = assimp_mesh->mNormals[i];
					vertex.nor[0] = normal.x;
					vertex.nor[1] = normal.y;
					vertex.nor[2] = normal.z;
				}

				// Tangent
				if (assimp_mesh->mTangents)
				{
					const auto& tangent = assimp_mesh->mTangents[i];
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
		vector<uint32_t> indices;
		{
			// Pre-allocate for extra performance
			const auto index_count = assimp_mesh->mNumFaces * 3;
			indices.reserve(index_count);
			indices.resize(index_count);

			// Get indices by iterating through each face of the mesh.
			for (uint32_t face_index = 0; face_index < assimp_mesh->mNumFaces; face_index++)
			{
				// if (aiPrimitiveType_LINE | aiPrimitiveType_POINT) && aiProcess_Triangulate) then (face.mNumIndices == 3)
				auto& face					= assimp_mesh->mFaces[face_index];
				const auto indices_index	= (face_index * 3);
				indices[indices_index + 0]	= face.mIndices[0];
				indices[indices_index + 1]	= face.mIndices[1];
				indices[indices_index + 2]	= face.mIndices[2];
			}
		}

		// Compute AABB (before doing move operation on vertices)
		const auto aabb = BoundingBox(vertices);

		// Add the mesh to the model
		uint32_t index_offset;
		uint32_t vertex_offset;
		model->GeometryAppend(move(indices), move(vertices), &index_offset, &vertex_offset);

		// Add a renderable component to this entity
		auto renderable	= entity_parent->AddComponent<Renderable>();

		// Set the geometry
		renderable->GeometrySet(
			entity_parent->GetName(),
			index_offset,
			static_cast<uint32_t>(indices.size()),
			vertex_offset,
			static_cast<uint32_t>(vertices.size()),
			aabb,
			model
		);

		// Material
		if (assimp_scene->HasMaterials())
		{
			// Get aiMaterial
			const auto assimp_material = assimp_scene->mMaterials[assimp_mesh->mMaterialIndex];
			// Convert it and add it to the model
			model->AddMaterial(AiMaterialToMaterial(assimp_material, model), entity_parent->GetPtrShared());
		}

		// Bones
		//for (uint32_t boneIndex = 0; boneIndex < assimpMesh->mNumBones; boneIndex++)
		//{
			//aiBone* bone = assimpMesh->mBones[boneIndex];
		//}
	}

	shared_ptr<Material> ModelImporter::AiMaterialToMaterial(aiMaterial* assimp_material, Model* model)
	{
		if (!model || !assimp_material)
		{
			LOG_WARNING("One of the provided materials is null, can't execute function");
			return nullptr;
		}

		auto material = make_shared<Material>(m_context);

		// NAME
		aiString name;
		aiGetMaterialString(assimp_material, AI_MATKEY_NAME, &name);
		material->SetResourceName(name.C_Str());

		// CULL MODE
		// Specifies whether meshes using this material must be rendered 
		// without back face CullMode. 0 for false, !0 for true.
		auto is_two_sided	= 0;
		uint32_t max	= 1;
		if (AI_SUCCESS == aiGetMaterialIntegerArray(assimp_material, AI_MATKEY_TWOSIDED, &is_two_sided, &max))
		{
			if (is_two_sided != 0)
			{
				material->SetCullMode(Cull_None);
			}
		}

		// DIFFUSE COLOR
		aiColor4D color_diffuse(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_DIFFUSE, &color_diffuse);
		
		// OPACITY
		aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
		aiGetMaterialColor(assimp_material, AI_MATKEY_OPACITY, &opacity);

		material->SetColorAlbedo(Vector4(color_diffuse.r, color_diffuse.g, color_diffuse.b, opacity.r));

		// TEXTURES
		const auto load_mat_tex = [&model, &assimp_material, &material](const aiTextureType type_assimp, const TextureType type_spartan)
		{
			aiString texture_path;
			if (assimp_material->GetTextureCount(type_assimp) > 0)
			{
				if (AI_SUCCESS == assimp_material->GetTexture(type_assimp, 0, &texture_path))
				{
					const auto deduced_path = AssimpHelper::texture_validate_path(texture_path.data, _ModelImporter::m_model_path);
					if (FileSystem::IsSupportedImageFile(deduced_path))
					{
						model->AddTexture(material, type_spartan, AssimpHelper::texture_validate_path(texture_path.data, _ModelImporter::m_model_path));

						if (type_assimp == aiTextureType_DIFFUSE)
						{
							// FIX: materials that have a diffuse texture should not be tinted black/gray
							material->SetColorAlbedo(Vector4::One);
						}

						// Some models (or Assimp) pass a normal map as a height map
						// auto textureType others pass a height map as a normal map, we try to fix that.
						if (type_spartan == TextureType_Normal || type_spartan == TextureType_Height)
						{
							const auto texture = material->GetTexture(type_spartan);							
							auto proper_type = type_spartan;
							proper_type = (proper_type == TextureType_Normal && texture->GetGrayscale()) ? TextureType_Height : proper_type;
							proper_type = (proper_type == TextureType_Height && !texture->GetGrayscale()) ? TextureType_Normal : proper_type;

							if (proper_type != type_spartan)
							{
								material->SetTextureSlot(type_spartan, shared_ptr<RHI_Texture>());
								material->SetTextureSlot(proper_type, texture);
							}
						}
					}
				}
			}
		};
		
		load_mat_tex(aiTextureType_DIFFUSE,		TextureType_Albedo);
		load_mat_tex(aiTextureType_SHININESS,	TextureType_Roughness); // Specular as roughness
		load_mat_tex(aiTextureType_AMBIENT,		TextureType_Metallic);	// Ambient as metallic
		load_mat_tex(aiTextureType_NORMALS,		TextureType_Normal);
		load_mat_tex(aiTextureType_LIGHTMAP,	TextureType_Occlusion);
		load_mat_tex(aiTextureType_EMISSIVE,	TextureType_Emission);
		load_mat_tex(aiTextureType_LIGHTMAP,	TextureType_Occlusion);
		load_mat_tex(aiTextureType_HEIGHT,		TextureType_Height);
		load_mat_tex(aiTextureType_OPACITY,		TextureType_Mask);

		return material;
	}
}
