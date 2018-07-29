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
#include "../../RHI/IRHI_Implementation.h"
#include "../../Math/Vector3.h"
#include "../../Resource/ResourceManager.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Skybox::Skybox(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{

	}

	Skybox::~Skybox()
	{
		 Getactor_PtrRaw()->RemoveComponent<Renderable>();
	}

	void Skybox::OnInitialize()
	{
		// Load environment texture and create a cubemap
		auto cubemapDirectory	= GetContext()->GetSubsystem<ResourceManager>()->GetStandardResourceDirectory(Resource_Cubemap);
		auto texPath			= cubemapDirectory + "environment.dds";
		m_cubemapTexture		= make_shared<RHI_Texture>(GetContext());
		m_cubemapTexture->SetResourceName("Cubemap");
		m_cubemapTexture->LoadFromFile(texPath);
		m_cubemapTexture->SetType(TextureType_CubeMap);
		m_cubemapTexture->SetWidth(1024);
		m_cubemapTexture->SetHeight(1024);
		m_cubemapTexture->SetGrayscale(false);
		
		// Create a skybox material
		m_matSkybox = make_shared<Material>(GetContext());
		m_matSkybox->SetResourceName("Standard_Skybox");
		m_matSkybox->SetCullMode(Cull_Front);
		m_matSkybox->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_matSkybox->SetIsEditable(false);
		m_matSkybox->SetTexture(m_cubemapTexture, false); // assign cubmap texture

		// Add a Renderable and assign the skybox material to it
		auto renderable = Getactor_PtrRaw()->AddComponent<Renderable>().lock();
		renderable->Geometry_Set(Geometry_Default_Cube);
		renderable->SetCastShadows(false);
		renderable->SetReceiveShadows(false);
		renderable->Material_Set(m_matSkybox, true);

		// Make the skybox big enough
		GetTransform()->SetScale(Vector3(1000, 1000, 1000));
	}

	void Skybox::OnUpdate()
	{

	}

	void* Skybox::GetShaderResource()
	{
		return m_cubemapTexture ? m_cubemapTexture->GetShaderResource() : nullptr;
	}
}