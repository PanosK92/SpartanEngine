/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===========================
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <vector>
#include "ModelImporter.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/MeshFilter.h"
#include "../Core/GameObject.h"
#include "../Pools/MaterialPool.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

// default pp steps
static auto ppsteps =
aiProcess_CalcTangentSpace |
aiProcess_GenSmoothNormals |
aiProcess_JoinIdenticalVertices |
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
aiProcess_OptimizeMeshes |
aiProcess_Debone |
aiProcess_ConvertToLeftHanded;

static int smoothAngle = 80;

ModelImporter::ModelImporter()
{
	m_rootGameObject = nullptr;
	m_meshPool = nullptr;
	m_texturePool = nullptr;
	m_shaderPool = nullptr;
}

ModelImporter::~ModelImporter()
{
}

void ModelImporter::Initialize(MeshPool* meshPool, TexturePool* texturePool, ShaderPool* shaderPool, MaterialPool* materialPool)
{
	m_meshPool = meshPool;
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_materialPool = materialPool;
}

bool ModelImporter::Load(string filePath, GameObject* gameObject)
{
	m_fullModelPath = filePath;
	m_rootGameObject = gameObject;

	Assimp::Importer importer;
	importer.SetPropertyInteger(AI_CONFIG_PP_ICL_PTCACHE_SIZE, 64); // Optimize mesh
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT); // Remove points and lines.
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS); // Remove cameras and lights
	importer.SetPropertyInteger(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, smoothAngle);

	const aiScene* scene = importer.ReadFile(m_fullModelPath, ppsteps);
	if (!scene) // Someting went wrong. Print it.
	{
		LOG_ERROR("Failed to load \"" + FileSystem::GetFileNameNoExtensionFromPath(m_fullModelPath) + "\". " + importer.GetErrorString());
		return false;
	}

	string name = FileSystem::GetFileNameNoExtensionFromPath(filePath);
	gameObject->SetName(name);

	// This function will recursively process the entire model
	ProcessNode(scene->mRootNode, scene, gameObject);

	// Normalize the scale of the model
	m_meshPool->NormalizeModelScale(m_rootGameObject);

	return true;
}

//= HELPER FUNCTIONS ========================================================================
Matrix aiMatrix4x4ToMatrix(const aiMatrix4x4& transform)
{
	// row major to column major
	return Matrix(
		transform.a1, transform.b1, transform.c1, transform.d1,
		transform.a2, transform.b2, transform.c2, transform.d2,
		transform.a3, transform.b3, transform.c3, transform.d3,
		transform.a4, transform.b4, transform.c4, transform.d4);
}

void SetGameObjectTransform(GameObject* gameObject, const aiMatrix4x4& assimpTransformation)
{
	Vector3 position;
	Quaternion rotation;
	Vector3 scale;

	Matrix matrix = aiMatrix4x4ToMatrix(assimpTransformation);
	matrix.Decompose(scale, rotation, position);

	// apply transformation
	gameObject->GetTransform()->SetPositionLocal(position);
	gameObject->GetTransform()->SetRotationLocal(rotation);
	gameObject->GetTransform()->SetScaleLocal(scale);
}

Vector4 ToVector4(const aiColor4D& aiColor)
{
	return Vector4(aiColor.r, aiColor.g, aiColor.b, aiColor.a);
}

Vector3 ToVector3(const aiVector3D& aiVector)
{
	return Vector3(aiVector.x, aiVector.y, aiVector.z);
}

Vector2 ToVector2(const aiVector2D& aiVector)
{
	return Vector2(aiVector.x, aiVector.y);
}
//============================================================================================

/*------------------------------------------------------------------------------
								[PROCESSING]
------------------------------------------------------------------------------*/

void ModelImporter::ProcessNode(aiNode* node, const aiScene* scene, GameObject* parentGameObject)
{
	// process root node
	if (!node->mParent)
	{
		SetGameObjectTransform(parentGameObject, node->mTransformation); // apply transformation	

		// node->mName always returns "RootNode", therefore the modelName has to be extracted from the path
		m_modelName = FileSystem::GetFileNameFromPath(m_fullModelPath);
	}

	// process all the node's meshes
	for (auto i = 0; i < node->mNumMeshes; i++)
	{
		GameObject* gameobject = parentGameObject; // set the current gameobject
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]]; // get mesh
		string name = node->mName.C_Str(); // get name

		// if this node has many meshes, then assign  a new gameobject for each one of them
		if (node->mNumMeshes > 1)
		{
			gameobject = new GameObject(); // create
			gameobject->GetTransform()->SetParent(parentGameObject->GetTransform()); // set parent
			name += "_" + to_string(i + 1); // set name
		}

		// set gameobject's name
		gameobject->SetName(name);

		// process mesh
		//aiMatrix4x4 transformation = node->mTransformation;
		ProcessMesh(mesh, scene, gameobject);
	}

	// process child nodes (if any)
	for (auto i = 0; i < node->mNumChildren; i++)
	{
		aiNode* childNode = node->mChildren[i]; // get  node

		GameObject* gameobject = new GameObject(); // create
		gameobject->GetTransform()->SetParent(parentGameObject->GetTransform()); // set parent
		gameobject->SetName(childNode->mName.C_Str()); // set name
		SetGameObjectTransform(gameobject, childNode->mTransformation);

		// continue processing recursively
		ProcessNode(childNode, scene, gameobject);
	}
}

void ModelImporter::ProcessMesh(aiMesh* mesh, const aiScene* scene, GameObject* gameobject)
{
	vector<VertexPositionTextureNormalTangent> vertices;
	vector<unsigned int> indices;

	VertexPositionTextureNormalTangent vertex;
	for (auto vertexIndex = 0; vertexIndex < mesh->mNumVertices; vertexIndex++)
	{
		// get the position
		vertex.position = ToVector3(mesh->mVertices[vertexIndex]);

		// get the normal
		if (NULL != mesh->mNormals)
			vertex.normal = ToVector3(mesh->mNormals[vertexIndex]);

		// get the tangent
		if (NULL != mesh->mTangents)
			vertex.tangent = ToVector3(mesh->mTangents[vertexIndex]);

		// get the texture coordinates
		if (mesh->HasTextureCoords(0))
			vertex.uv = ToVector2(aiVector2D(mesh->mTextureCoords[0][vertexIndex].x, mesh->mTextureCoords[0][vertexIndex].y));

		// save the vertex
		vertices.push_back(vertex);

		// reset the vertex for use in the next loop
		vertex.normal = Vector3::Zero;
		vertex.tangent = Vector3::Zero;
		vertex.uv = Vector2::Zero;
	}

	// get the indices by iterating through each face of the mesh.
	for (auto i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];

		if (face.mNumIndices < 3)
			continue;

		for (auto j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// Add a mesh component and pass the data
	MeshFilter* meshComp = gameobject->AddComponent<MeshFilter>();
	meshComp->Set(mesh->mName.C_Str(), m_rootGameObject->GetID(), vertices, indices);

	// process materials
	if (scene->HasMaterials())
	{
		// Get assimp material
		aiMaterial* assimpMaterial = scene->mMaterials[mesh->mMaterialIndex];

		// Convert it
		Material* material = GenerateMaterialFromAiMaterial(assimpMaterial);

		// Add it to the material pool
		material = m_materialPool->AddMaterial(material);

		// Set it in the mesh renderer component
		gameobject->AddComponent<MeshRenderer>()->SetMaterial(material->GetID());
	}

	// free memory
	vertices.clear();
	indices.clear();
}

Material* ModelImporter::GenerateMaterialFromAiMaterial(aiMaterial* material)
{
	Material* engineMaterial = new Material(m_texturePool, m_shaderPool);

	//= NAME ====================================================================
	aiString name;
	aiGetMaterialString(material, AI_MATKEY_NAME, &name);
	engineMaterial->SetName(name.C_Str());
	engineMaterial->SetModelID(m_modelName);

	//= CullMode ===============================================================================================
	// Specifies whether meshes using this material must be rendered without backface CullMode. 0 for false, !0 for true.
	bool isTwoSided = false;
	int r = material->Get(AI_MATKEY_TWOSIDED, isTwoSided);
	if (r == aiReturn_SUCCESS && isTwoSided)
	{
		LOG("two-sided");
		engineMaterial->SetFaceCullMode(CullNone);
	}

	//= DIFFUSE COLOR ======================================================================================
	aiColor4D colorDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
	aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &colorDiffuse);
	engineMaterial->SetColorAlbedo(ToVector4(colorDiffuse));

	//= OPACITY ==============================================
	aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
	aiGetMaterialColor(material, AI_MATKEY_OPACITY, &opacity);
	engineMaterial->SetOpacity(opacity.r);

	//= ALBEDO TEXTURE ======================================================================================================
	aiString Path;
	if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
	{
		// FIX: materials that have a diffuse texture should not be tinted black
		if (engineMaterial->GetColorAlbedo() == Vector4(0.0f, 0.0f, 0.0f, 1.0f))
			engineMaterial->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

		// Get the full texture path.
		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				Texture* texture = m_texturePool->AddFromFile(path, Albedo);
				engineMaterial->SetTexture(texture->GetID());
			}
			else
				LOG_WARNING("Failed to find \"" + FileSystem::GetFileNameFromPath(string(Path.data)) + "\".");
		}
	}

	//= OCCLUSION TEXTURE ====================================================================================================
	if (material->GetTextureCount(aiTextureType_LIGHTMAP) > 0)
		if (material->GetTexture(aiTextureType_LIGHTMAP, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				Texture* texture = m_texturePool->AddFromFile(path, Occlusion);
				engineMaterial->SetTexture(texture->GetID());
			}
			else
				LOG_WARNING("Failed to find \"" + FileSystem::GetFileNameFromPath(string(Path.data)) + "\".");
		}

	//= NORMAL TEXTURE ======================================================================================================
	if (material->GetTextureCount(aiTextureType_NORMALS) > 0)
		if (material->GetTexture(aiTextureType_NORMALS, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				Texture* texture = m_texturePool->AddFromFile(path, Normal);
				engineMaterial->SetTexture(texture->GetID());
			}
			else
				LOG_WARNING("Failed to find \"" + FileSystem::GetFileNameFromPath(string(Path.data)) + "\".");
		}

	//= HEIGHT TEXTURE =====================================================================================================
	if (material->GetTextureCount(aiTextureType_HEIGHT) > 0)
		if (material->GetTexture(aiTextureType_HEIGHT, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				Texture* texture = m_texturePool->AddFromFile(path, Height);
				engineMaterial->SetTexture(texture->GetID());
			}
			else
				LOG_WARNING("Failed to find \"" + FileSystem::GetFileNameFromPath(string(Path.data)) + "\".");
		}

	//= MASK TEXTURE ========================================================================================================
	if (material->GetTextureCount(aiTextureType_OPACITY) > 0)
		if (material->GetTexture(aiTextureType_OPACITY, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				Texture* texture = m_texturePool->AddFromFile(path, Mask);
				engineMaterial->SetTexture(texture->GetID());
			}
			else
				LOG_WARNING("Failed to find \"" + FileSystem::GetFileNameFromPath(string(Path.data)) + "\".");
		}

	return engineMaterial;
}

/*------------------------------------------------------------------------------
[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
// The texture path is relative to the model directory and the model path is absolute...
// This methods constructs a path relative to the engine based on the above paths.
string ModelImporter::ConstructRelativeTexturePath(string absoluteTexturePath)
{
	// Save original texture path;
	m_fullTexturePath = absoluteTexturePath;

	// Remove the model's filename from the model path
	string absoluteModelPath = m_fullModelPath.substr(0, m_fullModelPath.find_last_of("\\/"));

	// Remove everything before the folder "Assets", making the path relative to the engine
	size_t position = absoluteModelPath.find("Assets");
	string relativeModelPath = absoluteModelPath.substr(position);

	// Construct the final relative texture path
	string relativeTexturePath = relativeModelPath + "/" + absoluteTexturePath;

	return relativeTexturePath;
}

string ModelImporter::FindTexture(string texturePath) const
{
	if (FileSystem::FileExists(texturePath))
		return texturePath;

	//= try path as is but with multiple extensions ===========
	texturePath = TryPathWithMultipleExtensions(texturePath);
	if (FileSystem::FileExists(texturePath))
		return texturePath;
	//=========================================================

	//= try path as filename only, with multiple extensions ====
	string filename = FileSystem::GetFileNameFromPath(m_fullTexturePath);

	// get model's root directory.
	string modelPath = m_fullModelPath;
	string path = FileSystem::GetPathWithoutFileName(modelPath);

	// combine them
	string newPath = path + filename;
	newPath = TryPathWithMultipleExtensions(newPath);
	if (FileSystem::FileExists(newPath))
		return newPath;
	//==========================================================

	return TEXTURE_PATH_UNKNOWN;
}

string ModelImporter::TryPathWithMultipleExtensions(string fullpath)
{
	// Remove extension
	int lastindex = fullpath.find_last_of(".");
	string rawPath = fullpath.substr(0, lastindex);

	// create path for a couple of different extensions
	const int extensions = 12;
	string multipleExtensionPaths[extensions] =
	{
		rawPath + ".jpg",
		rawPath + ".png",
		rawPath + ".bmp",
		rawPath + ".tga",
		rawPath + ".dds",
		rawPath + ".psd",

		rawPath + ".JPG",
		rawPath + ".PNG",
		rawPath + ".BMP",
		rawPath + ".TGA",
		rawPath + ".DDS",
		rawPath + ".PSD",
	};

	for (int i = 0; i < extensions; i++)
		if (FileSystem::FileExists(multipleExtensionPaths[i]))
			return multipleExtensionPaths[i];

	return fullpath;
}
