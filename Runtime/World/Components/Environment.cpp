/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===================================
#include "Runtime/Core/Spartan.h"
#include "Environment.h"
#include "../../IO/FileStream.h"
#include "../../Threading/Threading.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../Resource/ResourceCache.h"
#include "../../Resource/Import/ImageImporter.h"
#include "../../Rendering/Renderer.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Environment::Environment(Context* context, Entity* entity, uint64_t id /*= 0*/) : IComponent(context, entity, id)
    {
        // Default texture paths
        const string dir_cubemaps = GetContext()->GetSubsystem<ResourceCache>()->GetResourceDirectory(ResourceDirectory::Cubemaps) + "/";
        if (m_environment_type == EnvironmentType::Cubemap)
        {
            m_file_paths =
            {
                dir_cubemaps + "array/X+.tga", // right
                dir_cubemaps + "array/X-.tga", // left
                dir_cubemaps + "array/Y+.tga", // up
                dir_cubemaps + "array/Y-.tga", // down
                dir_cubemaps + "array/Z-.tga", // back
                dir_cubemaps + "array/Z+.tga"  // front
            };
        }
        else if (m_environment_type == EnvironmentType::Sphere)
        {
            m_file_paths = { dir_cubemaps + "syferfontein_0d_clear_4k.hdr" };
        }     
    }

    void Environment::OnTick(double delta_time)
    {
        if (m_is_dirty)
        {
            m_context->GetSubsystem<Threading>()->AddTask([this]()
            {
                SetFromTextureSphere(m_file_paths.front());
            });
            
            m_is_dirty = false;
        }
    }

    void Environment::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint8_t>(m_environment_type));
        stream->Write(m_file_paths);
    }

    void Environment::Deserialize(FileStream* stream)
    {
        m_environment_type = static_cast<EnvironmentType>(stream->ReadAs<uint8_t>());
        stream->Read(&m_file_paths);

        m_context->GetSubsystem<Threading>()->AddTask([this]
        {
            if (m_environment_type == EnvironmentType::Cubemap)
            {
                SetFromTextureArray(m_file_paths);
                
            }
            else if (m_environment_type == EnvironmentType::Sphere)
            {
                
                SetFromTextureSphere(m_file_paths.front());
            }
        });
    }

    const shared_ptr<Spartan::RHI_Texture> Environment::GetTexture() const
    {
        return m_context->GetSubsystem<Renderer>()->GetEnvironmentTexture();
    }

    void Environment::SetTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_context->GetSubsystem<Renderer>()->SetEnvironmentTexture(texture);
    }

    void Environment::SetFromTextureArray(const vector<string>& file_paths)
    {
        if (file_paths.empty())
            return;

        LOG_INFO("Loading sky box...");

        ResourceCache* resource_cache = m_context->GetSubsystem<ResourceCache>();

        // Load all textures (sides)
        shared_ptr<RHI_Texture> texture = make_shared<RHI_TextureCube>(GetContext());
        for (uint32_t slice_index = 0; static_cast<uint32_t>(file_paths.size()); slice_index++)
        {
            resource_cache->GetImageImporter()->Load(file_paths[slice_index], slice_index, static_cast<RHI_Texture*>(texture.get()));
        }

        // Set resource file path
        texture->SetResourceFilePath(resource_cache->GetProjectDirectory() + "environment" + EXTENSION_TEXTURE);

        // Save file path for serialization/deserialization
        m_file_paths = { texture->GetResourceFilePath() };

        // Pass the texture to the renderer.
        SetTexture(texture);

        LOG_INFO("Sky box has been created successfully");
    }

    void Environment::SetFromTextureSphere(const string& file_path)
    {
        LOG_INFO("Loading sky sphere...");

        // Create texture
        shared_ptr<RHI_Texture> texture = make_shared<RHI_Texture2D>(GetContext(), RHI_Texture_Srv | RHI_Texture_Mips);

        if (!texture->LoadFromFile(file_path))
        {
            LOG_ERROR("Sky sphere creation failed");
        }

        // Save file path for serialization/deserialization
        m_file_paths = { texture->GetResourceFilePath() };
        
        // Pass the texture to the renderer.
        SetTexture(texture);
        
        LOG_INFO("Sky sphere has been created successfully");
    }
}
