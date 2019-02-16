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

//= INCLUDES ============================
#include "Skybox.h"
#include "Transform.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../../Resource/ResourceCache.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Math/MathHelper.h"
#include "../../Rendering/Material.h"
//=======================================

//= NAMESPACES ========================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=====================================

namespace Directus
{
	Skybox::Skybox(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_skyboxType		= Skybox_Sphere;
		m_cubemapTexture	= make_shared<RHI_Texture>(GetContext());
		m_matSkybox			= make_shared<Material>(GetContext());
		m_matSkybox->SetCullMode(Cull_Front);
		m_matSkybox->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_matSkybox->SetIsEditable(false);
		m_matSkybox->SetShadingMode(Material::Shading_Sky);
		
		// Texture paths
		auto cubemapDirectory = GetContext()->GetSubsystem<ResourceCache>()->GetStandardResourceDirectory(Resource_Cubemap);
		if (m_skyboxType == Skybox_Array)
		{
			m_texturePaths =
			{
				cubemapDirectory + "array/X+.tga",	// right
				cubemapDirectory + "array/X-.tga",	// left
				cubemapDirectory + "array/Y+.tga",	// up
				cubemapDirectory + "array/Y-.tga",	// down
				cubemapDirectory + "array/Z-.tga",	// back
				cubemapDirectory + "array/Z+.tga"	// front
			};
		}
		else if (m_skyboxType == Skybox_Sphere)
		{
			m_texturePaths = { cubemapDirectory + "sphere/syferfontein_0d_clear_4k.hdr" };
		}
	}

	Skybox::~Skybox()
	{
		 
	}

	void Skybox::OnInitialize()
	{
		if (m_skyboxType == Skybox_Array)
		{
			CreateFromArray(m_texturePaths);
		}
		else if (m_skyboxType == Skybox_Sphere)
		{
			CreateFromSphere(m_texturePaths.front());
		}
	}

	void Skybox::OnTick()
	{

	}

	void Skybox::CreateFromArray(const vector<string>& texturePaths)
	{
		if (texturePaths.empty())
			return;

		// Load all textures (sides)
		vector<vector<vector<std::byte>>> cubemapData;

		// Load all the cubemap sides
		auto loaderTex = make_shared<RHI_Texture>(GetContext());
		{
			loaderTex->LoadFromFile(texturePaths[0]);
			cubemapData.emplace_back(loaderTex->Data_Get());

			loaderTex->LoadFromFile(texturePaths[1]);
			cubemapData.emplace_back(loaderTex->Data_Get());

			loaderTex->LoadFromFile(texturePaths[2]);
			cubemapData.emplace_back(loaderTex->Data_Get());

			loaderTex->LoadFromFile(texturePaths[3]);
			cubemapData.emplace_back(loaderTex->Data_Get());

			loaderTex->LoadFromFile(texturePaths[4]);
			cubemapData.emplace_back(loaderTex->Data_Get());

			loaderTex->LoadFromFile(texturePaths[5]);
			cubemapData.emplace_back(loaderTex->Data_Get());
		}

		// Cubemap
		{
			m_cubemapTexture->ShaderResource_CreateCubemap(loaderTex->GetWidth(), loaderTex->GetHeight(), loaderTex->GetChannels(), loaderTex->GetFormat(), cubemapData);
			m_cubemapTexture->SetResourceName("Cubemap");
			m_cubemapTexture->SetWidth(loaderTex->GetWidth());
			m_cubemapTexture->SetHeight(loaderTex->GetHeight());
			m_cubemapTexture->SetGrayscale(false);
		}

		// Material
		{
			m_matSkybox->SetResourceName("Standard_Skybox");
			m_matSkybox->SetTextureSlot(TextureType_Albedo, m_cubemapTexture);
		}

		// Renderable
		{
			auto renderable = GetEntity_PtrRaw()->AddComponent<Renderable>();
			renderable->Geometry_Set(Geometry_Default_Cube);
			renderable->SetCastShadows(false);
			renderable->SetReceiveShadows(false);
			renderable->Material_Set(m_matSkybox);
		}

		// Make the skybox big enough
		GetTransform()->SetScale(Vector3(1000, 1000, 1000));
	}

	void Skybox::CreateFromSphere(const string& texturePath)
	{
		// Texture
		{
			m_cubemapTexture = make_shared<RHI_Texture>(GetContext());
			m_cubemapTexture->LoadFromFile(texturePath);
			m_cubemapTexture->SetResourceName("Skysphere");
		}

		// Material
		{
			m_matSkybox->SetResourceName("Standard_Skysphere");
			m_matSkybox->SetTextureSlot(TextureType_Albedo, m_cubemapTexture);
		}

		// Renderable
		{
			auto renderable = GetEntity_PtrRaw()->AddComponent<Renderable>();
			renderable->Geometry_Set(Geometry_Default_Sphere);
			renderable->SetCastShadows(false);
			renderable->SetReceiveShadows(false);
			renderable->Material_Set(m_matSkybox);
		}

		// Make the skybox big enough
		GetTransform()->SetScale(Vector3(980, 980, 980));
	}
}