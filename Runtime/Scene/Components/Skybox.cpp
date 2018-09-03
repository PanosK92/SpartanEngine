/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ==============================
#include "Skybox.h"
#include "Transform.h"
#include "Renderable.h"
#include "../Actor.h"
#include "../../Resource/ResourceManager.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Math/MathHelper.h"
//=========================================

//= NAMESPACES ========================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Math::Helper;
//=====================================

namespace Directus
{
	Skybox::Skybox(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{
		m_cubemapTexture	= make_shared<RHI_Texture>(GetContext());
		m_matSkybox			= make_shared<Material>(GetContext());
		m_format			= Texture_Format_R8G8B8A8_UNORM;
		m_skyboxType		= Skybox_Array;

		// Texture paths
		auto cubemapDirectory = GetContext()->GetSubsystem<ResourceManager>()->GetStandardResourceDirectory(Resource_Cubemap);
		if (m_skyboxType == Skybox_Array)
		{
			m_texturePaths =
			{
				cubemapDirectory + "hw_morning/X+.tga",	// right
				cubemapDirectory + "hw_morning/X-.tga",	// left
				cubemapDirectory + "hw_morning/Y+.tga",	// up
				cubemapDirectory + "hw_morning/Y-.tga",	// down
				cubemapDirectory + "hw_morning/Z-.tga",	// back
				cubemapDirectory + "hw_morning/Z+.tga"	// front
			};
		}
		else if (m_skyboxType == Skybox_Cross)
		{
			m_texturePaths = { cubemapDirectory + "cross.jpg" };
		}
	}

	Skybox::~Skybox()
	{
		 GetActor_PtrRaw()->RemoveComponent<Renderable>();
	}

	void Skybox::OnInitialize()
	{
		if (m_skyboxType == Skybox_Array)
		{
			CreateFromArray(m_texturePaths);
		}
		else if (m_skyboxType == Skybox_Cross)
		{
			CreateFromCross(m_texturePaths.front());
		}
	}

	void Skybox::OnTick()
	{

	}

	void Skybox::CreateFromArray(const vector<string>& texturePaths)
	{
		// Load all textures (sides) in a different thread to speed up engine start-up
		m_context->GetSubsystem<Threading>()->AddTask([this, &texturePaths]()
		{
			vector<vector<vector<std::byte>>> cubemapData;

			// Load all the cubemap sides
			auto loaderTex = make_shared<RHI_Texture>(GetContext());
			{
				loaderTex->LoadFromFile(texturePaths[0]);
				cubemapData.emplace_back(loaderTex->GetData());

				loaderTex->LoadFromFile(texturePaths[1]);
				cubemapData.emplace_back(loaderTex->GetData());

				loaderTex->LoadFromFile(texturePaths[2]);
				cubemapData.emplace_back(loaderTex->GetData());

				loaderTex->LoadFromFile(texturePaths[3]);
				cubemapData.emplace_back(loaderTex->GetData());

				loaderTex->LoadFromFile(texturePaths[4]);
				cubemapData.emplace_back(loaderTex->GetData());

				loaderTex->LoadFromFile(texturePaths[5]);
				cubemapData.emplace_back(loaderTex->GetData());
			}

			m_size		= loaderTex->GetWidth();
			m_format	= loaderTex->GetFormat();

			// Cubemap
			{
				m_cubemapTexture->ShaderResource_CreateCubemap(m_size, m_size, 4, m_format, cubemapData);
				m_cubemapTexture->SetResourceName("Cubemap");
				m_cubemapTexture->SetType(TextureType_CubeMap);
				m_cubemapTexture->SetWidth(m_size);
				m_cubemapTexture->SetHeight(m_size);
				m_cubemapTexture->SetGrayscale(false);
			}

			// Material
			{
				m_matSkybox->SetResourceName("Standard_Skybox");
				m_matSkybox->SetCullMode(Cull_Front);
				m_matSkybox->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				m_matSkybox->SetIsEditable(false);
				m_matSkybox->SetTexture(m_cubemapTexture, false); // assign cubmap texture
			}

			// Renderable
			{
				auto renderable = GetActor_PtrRaw()->AddComponent<Renderable>().lock();
				renderable->Geometry_Set(Geometry_Default_Cube);
				renderable->SetCastShadows(false);
				renderable->SetReceiveShadows(false);
				renderable->Material_Set(m_matSkybox, true);
			}

			// Make the skybox big enough
			GetTransform()->SetScale(Vector3(1000, 1000, 1000));
		});
	}

	void Skybox::CreateFromCross(const string& texturePath)
	{
		// Load all textures (sides) in a different thread to speed up engine start-up
		m_context->GetSubsystem<Threading>()->AddTask([this, &texturePath]()
		{
			// Load texture
			vector<mipmap> data; // vector<mip<data>>>
			auto texture = make_shared<RHI_Texture>(GetContext());
			texture->LoadFromFile(texturePath);
			data		= texture->GetData();
			m_format	= texture->GetFormat();
			m_size		= texture->GetHeight() / 3;

			// Split the cross into 6 individual textures
			vector<vector<mipmap>> cubemapData; // vector<texture<mip>>
			unsigned int mipWidth	= texture->GetWidth();
			unsigned int mipHeight	= texture->GetHeight();
			for (const auto& mip : data)
			{

				// Compute size of next mip-map
				mipWidth	= Max(mipWidth / 2, (unsigned int)1);
				mipHeight	= Max(mipHeight / 2, (unsigned int)1);
			}

			// Cubemap
			{
				m_cubemapTexture->ShaderResource_CreateCubemap(m_size, m_size, 4, m_format, cubemapData);
				m_cubemapTexture->SetResourceName("Cubemap");
				m_cubemapTexture->SetType(TextureType_CubeMap);
				m_cubemapTexture->SetWidth(m_size);
				m_cubemapTexture->SetHeight(m_size);
				m_cubemapTexture->SetGrayscale(false);
			}

			// Material
			{
				m_matSkybox->SetResourceName("Standard_Skybox");
				m_matSkybox->SetCullMode(Cull_Front);
				m_matSkybox->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				m_matSkybox->SetIsEditable(false);
				m_matSkybox->SetTexture(m_cubemapTexture, false); // assign cubmap texture
			}

			// Renderable
			{
				auto renderable = GetActor_PtrRaw()->AddComponent<Renderable>().lock();
				renderable->Geometry_Set(Geometry_Default_Cube);
				renderable->SetCastShadows(false);
				renderable->SetReceiveShadows(false);
				renderable->Material_Set(m_matSkybox, true);
			}

			// Make the skybox big enough
			GetTransform()->SetScale(Vector3(1000, 1000, 1000));
		});
	}
}