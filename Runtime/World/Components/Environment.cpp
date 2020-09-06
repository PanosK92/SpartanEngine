/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "Environment.h"
#include "../../IO/FileStream.h"
#include "../../Threading/Threading.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Renderer.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
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

        // Default texture paths
        const auto dir_cubemaps = GetContext()->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Cubemaps) + "/";
        if (m_environment_type == Enviroment_Cubemap)
        {
            m_file_paths =
            {
                dir_cubemaps + "array/X+.tga",    // right
                dir_cubemaps + "array/X-.tga",    // left
                dir_cubemaps + "array/Y+.tga",    // up
                dir_cubemaps + "array/Y-.tga",    // down
                dir_cubemaps + "array/Z-.tga",    // back
                dir_cubemaps + "array/Z+.tga"    // front
            };
        }
        else if (m_environment_type == Environment_Sphere)
        {
            m_file_paths = { dir_cubemaps + "syferfontein_0d_clear_4k.hdr" };
        }
    }

    void Environment::OnTick(float delta_time)
    {
        if (!m_is_dirty)
            return;

        m_context->GetSubsystem<Threading>()->AddTask([this]
        {
            SetFromTextureSphere(m_file_paths.front());
        });

        m_is_dirty = false;
    }

    void Environment::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint8_t>(m_environment_type));
        stream->Write(m_file_paths);
    }

    void Environment::Deserialize(FileStream* stream)
    {
        m_environment_type = static_cast<Environment_Type>(stream->ReadAs<uint8_t>());
        stream->Read(&m_file_paths);

        m_context->GetSubsystem<Threading>()->AddTask([this]
        {
            if (m_environment_type == Enviroment_Cubemap)
            {
                SetFromTextureArray(m_file_paths);
                
            }
            else if (m_environment_type == Environment_Sphere)
            {
                
                SetFromTextureSphere(m_file_paths.front());
            }
        });
    }

    void Environment::LoadDefault()
    {
        m_is_dirty = true;
    }

    const shared_ptr<RHI_Texture>& Environment::GetTexture() const
    {
        return m_context->GetSubsystem<Renderer>()->GetEnvironmentTexture();
    }

    void Environment::SetTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_context->GetSubsystem<Renderer>()->SetEnvironmentTexture(texture);

        // Save file path for serialization/deserialization
        m_file_paths = { texture ? texture->GetResourceFilePath() : "" };
    }

    void Environment::SetFromTextureArray(const vector<string>& file_paths)
    {
        if (file_paths.empty())
            return;

        LOG_INFO("Creating sky box...");

        // Load all textures (sides)
        vector<vector<vector<std::byte>>> cubemapData;

        // Load all the cubemap sides
        auto m_generate_mipmaps = false;
        auto loaderTex = make_shared<RHI_Texture2D>(GetContext(), m_generate_mipmaps);
        {
            loaderTex->LoadFromFile(file_paths[0]);
            cubemapData.emplace_back(loaderTex->GetMips());

            loaderTex->LoadFromFile(file_paths[1]);
            cubemapData.emplace_back(loaderTex->GetMips());

            loaderTex->LoadFromFile(file_paths[2]);
            cubemapData.emplace_back(loaderTex->GetMips());

            loaderTex->LoadFromFile(file_paths[3]);
            cubemapData.emplace_back(loaderTex->GetMips());

            loaderTex->LoadFromFile(file_paths[4]);
            cubemapData.emplace_back(loaderTex->GetMips());

            loaderTex->LoadFromFile(file_paths[5]);
            cubemapData.emplace_back(loaderTex->GetMips());
        }

        // Texture
        auto texture = make_shared<RHI_TextureCube>(GetContext(), loaderTex->GetWidth(), loaderTex->GetHeight(), loaderTex->GetFormat(), cubemapData);
        texture->SetResourceFilePath(m_context->GetSubsystem<ResourceCache>()->GetProjectDirectory() + "environment" + EXTENSION_TEXTURE);
        texture->SetWidth(loaderTex->GetWidth());
        texture->SetHeight(loaderTex->GetHeight());
        texture->SetGrayscale(false);

        // Apply sky sphere to renderer
        SetTexture(static_pointer_cast<RHI_Texture>(texture));

        LOG_INFO("Sky box has been created successfully");
    }

    void Environment::SetFromTextureSphere(const string& file_path)
    {
        LOG_INFO("Creating sky sphere...");

        // Don't generate mipmaps as the Renderer will generate a prefiltered environment which is required for proper IBL
        auto generate_mipmaps = true;

        // Skysphere
        auto texture = make_shared<RHI_Texture2D>(GetContext(), generate_mipmaps);
        if (texture->LoadFromFile(file_path))
        {
            // Set sky sphere to renderer
            SetTexture(static_pointer_cast<RHI_Texture>(texture));
            LOG_INFO("Sky sphere has been created successfully");
        }
        else
        {
            LOG_ERROR("Sky sphere creation failed");
        }
    }
}
