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
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Skybox::Skybox(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{
		auto cubemapDirectory = GetContext()->GetSubsystem<ResourceManager>()->GetStandardResourceDirectory(Resource_Cubemap);

		// Individual textures (sides)
		m_filePath_right	= cubemapDirectory + "hw_morning/X+.tga";
		m_filePath_left		= cubemapDirectory + "hw_morning/X-.tga";
		m_filepath_up		= cubemapDirectory + "hw_morning/Y+.tga";
		m_filePath_down		= cubemapDirectory + "hw_morning/Y-.tga";
		m_filePath_back		= cubemapDirectory + "hw_morning/Z-.tga";
		m_filePath_front	= cubemapDirectory + "hw_morning/Z+.tga";
		m_size				= 512;
		m_format			= Texture_Format_R8G8B8A8_UNORM;

		// Texture
		m_cubemapTexture = make_shared<RHI_Texture>(GetContext());

		// Material
		m_matSkybox = make_shared<Material>(GetContext());
	}

	Skybox::~Skybox()
	{
		 GetActor_PtrRaw()->RemoveComponent<Renderable>();
	}

	void Skybox::OnInitialize()
	{
		// Load all textures (sides) in a different thread to speed up engine start-up
		m_context->GetSubsystem<Threading>()->AddTask([this]()
		{
			vector<vector<std::byte>> cubemapData;

			// Load all the cubemap sides
			auto loaderTex = make_shared<RHI_Texture>(GetContext());
			{
				loaderTex->LoadFromFile(m_filePath_right);
				cubemapData.emplace_back(loaderTex->GetData().front());

				loaderTex->LoadFromFile(m_filePath_left);
				cubemapData.emplace_back(loaderTex->GetData().front());

				loaderTex->LoadFromFile(m_filepath_up);
				cubemapData.emplace_back(loaderTex->GetData().front());

				loaderTex->LoadFromFile(m_filePath_down);
				cubemapData.emplace_back(loaderTex->GetData().front());

				loaderTex->LoadFromFile(m_filePath_back);
				cubemapData.emplace_back(loaderTex->GetData().front());

				loaderTex->LoadFromFile(m_filePath_front);
				cubemapData.emplace_back(loaderTex->GetData().front());
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

	void Skybox::OnTick()
	{

	}
}