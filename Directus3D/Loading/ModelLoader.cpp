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
#include "ModelLoader.h"
#include "../IO/FileHelper.h"
#include "../IO/Log.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Mesh.h"
#include "../Core/GameObject.h"
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
aiProcess_ConvertToLeftHanded |
0;

ModelLoader::ModelLoader()
{
	m_rootGameObject = nullptr;
	m_meshPool = nullptr;
	m_texturePool = nullptr;
	m_shaderPool = nullptr;
}

ModelLoader::~ModelLoader()
{
}

void ModelLoader::Initialize(MeshPool* meshPool, TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_meshPool = meshPool;
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
}

bool ModelLoader::Load(string path, GameObject* gameObject)
{
	m_fullModelPath = path;
	m_rootGameObject = gameObject;

	Assimp::Importer importer;
	importer.SetPropertyInteger(AI_CONFIG_PP_ICL_PTCACHE_SIZE, 64); // Optimize mesh
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT); // Remove points and lines.
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_CAMERAS | aiComponent_LIGHTS); // Remove cameras and lights
	int smoothAngle = 80;
	importer.SetPropertyInteger(AI_CONFIG_PP_CT_MAX_SMOOTHING_ANGLE, smoothAngle);

	const aiScene* scene = importer.ReadFile(m_fullModelPath, ppsteps);

	if (!scene) // Someting went wrong. Print it.
	{
		LOG("Failed to load \"" + FileHelper::GetFileNameNoExtensionFromPath(m_fullModelPath) + "\". " + importer.GetErrorString(), Log::Error);
		return false;
	}

	// This function will recursively process the entire model
	ProcessNode(scene->mRootNode, scene, gameObject);

	// Normalize the scale of the model
	m_meshPool->NormalizeModelScale(m_rootGameObject);

	return true;
}

Matrix aiMatrix4x4ToMatrix(aiMatrix4x4 transform)
{
	// row major to column major
	return Matrix(
		transform.a1, transform.b1, transform.c1, transform.d1,
		transform.a2, transform.b2, transform.c2, transform.d2,
		transform.a3, transform.b3, transform.c3, transform.d3,
		transform.a4, transform.b4, transform.c4, transform.d4);
}

void SetGameObjectTransform(GameObject* gameObject, aiMatrix4x4 assimpTransformation)
{
	Vector3 position;
	Quaternion rotation;
	Vector3 scale;

	Matrix worldMatrix = aiMatrix4x4ToMatrix(assimpTransformation);
	worldMatrix.Decompose(scale, rotation, position);

	// apply transformation
	gameObject->GetTransform()->SetPositionLocal(position);
	gameObject->GetTransform()->SetRotationLocal(rotation);
	gameObject->GetTransform()->SetScaleLocal(scale);
}

/*------------------------------------------------------------------------------
								[PROCESSING]
------------------------------------------------------------------------------*/

void ModelLoader::ProcessNode(aiNode* node, const aiScene* scene, GameObject* parentGameObject)
{
	// process root node
	if (!node->mParent)
	{
		SetGameObjectTransform(parentGameObject, node->mTransformation); // apply transformation	

		// node->mName always returns "RootNode", therefore the modelName has to be extracted from the path
		m_modelName = FileHelper::GetFileNameFromPath(m_fullModelPath);
	}

	// process all the node's meshes
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
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
	for (unsigned int i = 0; i < node->mNumChildren; i++)
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

void ModelLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, GameObject* gameobject)
{
	vector<VertexPositionTextureNormalTangent> vertices;
	vector<unsigned int> indices;

	for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; vertexIndex++)
	{
		// vertex
		aiVector3D position = mesh->mVertices[vertexIndex];

		// normal
		aiVector3D normal = aiVector3D(0.0f, 0.0f, 0.0f);
		if (NULL != mesh->mNormals)
			normal = mesh->mNormals[vertexIndex];

		// tangent
		aiVector3D tangent = aiVector3D(0.0f, 0.0f, 0.0f);
		if (NULL != mesh->mTangents)
			tangent = mesh->mTangents[vertexIndex];

		// bitangent
		aiVector3D bitangent = aiVector3D(0.0f, 0.0f, 0.0f);
		if (NULL != mesh->mBitangents)
			bitangent = mesh->mBitangents[vertexIndex];

		// texture coordinates
		aiVector2D texture = aiVector2D(0.5f, 0.5f);
		if (mesh->HasTextureCoords(0))
			texture = aiVector2D(mesh->mTextureCoords[0][vertexIndex].x, mesh->mTextureCoords[0][vertexIndex].y);

		// fill the vertex
		VertexPositionTextureNormalTangent vertex;
		vertex.position.x = position.x;
		vertex.position.y = position.y;
		vertex.position.z = position.z;

		vertex.texture.x = texture.x;
		vertex.texture.y = texture.y;

		vertex.normal.x = normal.x;
		vertex.normal.y = normal.y;
		vertex.normal.z = normal.z;

		vertex.tangent.x = tangent.x;
		vertex.tangent.y = tangent.y;
		vertex.tangent.z = tangent.z;

		// save it
		vertices.push_back(vertex);
	}

	// get the indices by iterating through each face of the mesh.
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// Add a mesh component and pass the data
	Mesh* meshComp = gameobject->AddComponent<Mesh>();
	meshComp->Set(m_rootGameObject->GetID(), vertices, indices, mesh->mNumFaces);

	// process materials
	if (scene->HasMaterials())
	{
		aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
		gameobject->AddComponent<MeshRenderer>()->SetMaterial(GenerateMaterialFromAiMaterial(material));
	}

	// free memory
	vertices.clear();
	indices.clear();
}

shared_ptr<Material> ModelLoader::GenerateMaterialFromAiMaterial(aiMaterial* material)
{
	shared_ptr<Material> engineMaterial(new Material(m_texturePool, m_shaderPool));

	//= NAME ====================================================================
	aiString name;
	aiGetMaterialString(material, AI_MATKEY_NAME, &name);
	engineMaterial->SetName(name.C_Str());
	engineMaterial->SetModelID(m_modelName);

	//= CULLING ===============================================================================================
	// Specifies whether meshes using this material must be rendered without backface culling. 0 for false, !0 for true.
	unsigned int max = 1;
	int two_sided;
	if ((AI_SUCCESS == aiGetMaterialIntegerArray(material, AI_MATKEY_TWOSIDED, &two_sided, &max)) && two_sided)
		engineMaterial->SetFaceCulling(CullNone);
	else
		engineMaterial->SetFaceCulling(CullBack);

	//= DIFFUSE COLOR ======================================================================================
	aiColor4D colorDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
	aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &colorDiffuse);
	engineMaterial->SetColorAlbedo(Vector4(colorDiffuse.r, colorDiffuse.g, colorDiffuse.b, colorDiffuse.a));

	//= OPACITY ==============================================
	aiColor4D opacity(1.0f, 1.0f, 1.0f, 1.0f);
	aiGetMaterialColor(material, AI_MATKEY_OPACITY, &opacity);
	engineMaterial->SetOpacity(opacity.r);

	//= ALBEDO TEXTURE ======================================================================================================
	aiString Path;
	if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
	{
		// FIX: materials who have a diffuse texture should not be tinted black
		if (engineMaterial->GetColorAlbedo() == Vector4(0.0f, 0.0f, 0.0f, 1.0f))
			engineMaterial->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

		// Get the full texture path.
		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				shared_ptr<Texture> texture(new Texture());
				texture->LoadFromFile(path, Albedo);
				engineMaterial->AddTexture(texture);
			}
		}
	}

	//= OCCLUSION TEXTURE ====================================================================================================
	if (material->GetTextureCount(aiTextureType_LIGHTMAP) > 0)
		if (material->GetTexture(aiTextureType_LIGHTMAP, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				shared_ptr<Texture> texture(new Texture());
				texture->LoadFromFile(path, Occlusion);
				engineMaterial->AddTexture(texture);
			}
		}

	//= NORMAL TEXTURE ======================================================================================================
	if (material->GetTextureCount(aiTextureType_NORMALS) > 0)
		if (material->GetTexture(aiTextureType_NORMALS, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				shared_ptr<Texture> texture(new Texture());
				texture->LoadFromFile(path, Normal);
				engineMaterial->AddTexture(texture);
			}
		}

	//= HEIGHT TEXTURE =====================================================================================================
	if (material->GetTextureCount(aiTextureType_HEIGHT) > 0)
		if (material->GetTexture(aiTextureType_HEIGHT, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				shared_ptr<Texture> texture(new Texture());
				texture->LoadFromFile(path, Height);
				engineMaterial->AddTexture(texture);
			}
		}

	//= MASK TEXTURE ========================================================================================================
	if (material->GetTextureCount(aiTextureType_OPACITY) > 0)
		if (material->GetTexture(aiTextureType_OPACITY, 0, &Path, nullptr, nullptr, nullptr, nullptr, nullptr) == AI_SUCCESS)
		{
			string path = FindTexture(ConstructRelativeTexturePath(Path.data));
			if (path != TEXTURE_PATH_UNKNOWN)
			{
				shared_ptr<Texture> texture(new Texture());
				texture->LoadFromFile(path, Mask);
				engineMaterial->AddTexture(texture);
			}
		}

	return engineMaterial;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
// The texture path is relative to the model directory and the model path is absolute...
// This methods constructs a path relative to the engine based on the above paths.
string ModelLoader::ConstructRelativeTexturePath(string absoluteTexturePath)
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

string ModelLoader::FindTexture(string texturePath)
{
	if (FileHelper::FileExists(texturePath))
		return texturePath;

	//= try path as is but with multiple extensions ===========
	texturePath = TryPathWithMultipleExtensions(texturePath);
	if (FileHelper::FileExists(texturePath))
		return texturePath;
	//=========================================================

	//= try path as filename only, with multiple extensions ====
	string filename = FileHelper::GetFileNameFromPath(m_fullTexturePath);

	// get model's root directory.
	string modelPath = m_fullModelPath;
	string path = FileHelper::GetPathWithoutFileName(modelPath);

	// combine them
	string newPath = path + filename;
	newPath = TryPathWithMultipleExtensions(newPath);
	if (FileHelper::FileExists(newPath))
		return newPath;
	//==========================================================

	LOG("Failed to find \"" + filename + "\", some models can have absolute texture paths.", Log::Warning);

	return TEXTURE_PATH_UNKNOWN;
}

string ModelLoader::TryPathWithMultipleExtensions(string fullpath)
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
		if (FileHelper::FileExists(multipleExtensionPaths[i]))
			return multipleExtensionPaths[i];

	return fullpath;
}
