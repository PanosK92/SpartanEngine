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
#include "Environment.h"
#include "../../Resource/ResourceCache.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../Threading/Threading.h"
#include "../../Rendering/Renderer.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	Environment::Environment(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
	{
		m_environment_type = Environment_Sphere;

		// Texture paths
		const auto dir_cubemaps = GetContext()->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Cubemaps);
		if (m_environment_type == Enviroment_Cubemap)
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
		else if (m_environment_type == Environment_Sphere)
		{
			m_texture_paths = { dir_cubemaps + "syferfontein_0d_clear_4k.hdr" };
		}
	}

	void Environment::OnInitialize()
	{
		m_context->GetSubsystem<Threading>()->AddTask([this]
		{
			if (m_environment_type == Enviroment_Cubemap)
			{
                LOG_INFO("Creating sky box...");
				CreateFromArray(m_texture_paths);
                LOG_INFO("Sky box has been created successfully");
			}
			else if (m_environment_type == Environment_Sphere)
			{
                LOG_INFO("Creating sky sphere...");
				CreateFromSphere(m_texture_paths.front());
                LOG_INFO("Sky sphere has been created successfully");
			}
		});
	}

    const shared_ptr<RHI_Texture>& Environment::GetTexture()
    {
        return m_context->GetSubsystem<Renderer>()->GetEnvironmentTexture();
    }

    void Environment::SetTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_context->GetSubsystem<Renderer>()->SetEnvironmentTexture(static_pointer_cast<RHI_Texture>(texture));
    }

    void Environment::CreateFromArray(const vector<string>& texturePaths)
	{
		if (texturePaths.empty())
			return;

		// Load all textures (sides)
		vector<vector<vector<std::byte>>> cubemapData;

		// Load all the cubemap sides
        auto m_generate_mipmaps = false;
		auto loaderTex = make_shared<RHI_Texture2D>(GetContext(), m_generate_mipmaps);
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
        auto texture = make_shared<RHI_TextureCube>(GetContext(), loaderTex->GetWidth(), loaderTex->GetHeight(), loaderTex->GetFormat(), cubemapData);
        texture->SetResourceName("Cubemap");
        texture->SetWidth(loaderTex->GetWidth());
        texture->SetHeight(loaderTex->GetHeight());
        texture->SetGrayscale(false);

        // Apply sky sphere to renderer
        SetTexture(static_pointer_cast<RHI_Texture>(texture));
	}

	void Environment::CreateFromSphere(const string& texture_path)
	{
        // Don't generate mipmaps as the Renderer will generate a prefiltered environment which is required for proper IBL
        auto m_generate_mipmaps = true;

        // Skysphere
        auto texture = make_shared<RHI_Texture2D>(GetContext(), m_generate_mipmaps);
        texture->LoadFromFile(texture_path);

        // Apply sky sphere to renderer
        SetTexture(static_pointer_cast<RHI_Texture>(texture));
	}
}
