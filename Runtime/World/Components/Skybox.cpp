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
#include "../../Resource/ResourceCache.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../Threading/Threading.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	Skybox::Skybox(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
	{
		m_environment_type = Skybox_Sphere;

		// Texture paths
		const auto dir_cubemaps = GetContext()->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Cubemaps);
		if (m_environment_type == Skybox_Array)
		{
			m_texture_paths =
			{
				dir_cubemaps + "array/X+.tga",	// right
				dir_cubemaps + "array/X-.tga",	// left
				dir_cubemaps + "array/Y+.tga",	// up
				dir_cubemaps + "array/Y-.tga",	// down
				dir_cubemaps + "array/Z-.tga",	// back
				dir_cubemaps + "array/Z+.tga"	// front
			};
		}
		else if (m_environment_type == Skybox_Sphere)
		{
			m_texture_paths = { dir_cubemaps + "sphere/syferfontein_0d_clear_4k.hdr" };
		}
	}

	void Skybox::OnInitialize()
	{
		m_context->GetSubsystem<Threading>()->AddTask([this]
		{		
			if (m_environment_type == Skybox_Array)
			{
				CreateFromArray(m_texture_paths);
			}
			else if (m_environment_type == Skybox_Sphere)
			{
				CreateFromSphere(m_texture_paths.front());
			}
		});
	}

	void Skybox::CreateFromArray(const vector<string>& texturePaths)
	{
		if (texturePaths.empty())
			return;

		// Load all textures (sides)
		vector<vector<vector<std::byte>>> cubemapData;

		// Load all the cubemap sides
		auto loaderTex = make_shared<RHI_Texture2D>(GetContext(), true);
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

		// Cubemap
        m_texture = static_pointer_cast<RHI_Texture>(make_shared<RHI_TextureCube>(GetContext(), loaderTex->GetWidth(), loaderTex->GetHeight(), loaderTex->GetFormat(), cubemapData));
        m_texture->SetResourceName("Cubemap");
        m_texture->SetWidth(loaderTex->GetWidth());
        m_texture->SetHeight(loaderTex->GetHeight());
        m_texture->SetGrayscale(false);
	}

	void Skybox::CreateFromSphere(const string& texture_path)
	{
		LOG_INFO("Creating HDR sky sphere...");

        auto m_generate_mipmaps = true;
        m_texture = static_pointer_cast<RHI_Texture>(make_shared<RHI_Texture2D>(GetContext(), m_generate_mipmaps));
        m_texture->LoadFromFile(texture_path);
        m_texture->SetResourceName("SkySphere");

		LOG_INFO("Sky sphere has been created successfully");
	}
}
