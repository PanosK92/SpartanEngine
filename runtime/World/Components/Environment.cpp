/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "Environment.h"
#include "../../IO/FileStream.h"
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
    Environment::Environment(Entity* entity, uint64_t id /*= 0*/) : IComponent(entity, id)
    {
        const string environment_texture_directory = ResourceCache::GetResourceDirectory(ResourceDirectory::Environment) + "\\";

        // Default texture paths
        if (m_environment_type == EnvironmentType::Cubemap)
        {
            m_file_paths =
            {
                environment_texture_directory + "array\\X+.tga", // right
                environment_texture_directory + "array\\X-.tga", // left
                environment_texture_directory + "array\\Y+.tga", // up
                environment_texture_directory + "array\\Y-.tga", // down
                environment_texture_directory + "array\\Z-.tga", // back
                environment_texture_directory + "array\\Z+.tga"  // front
            };
        }
        else if (m_environment_type == EnvironmentType::Sphere)
        {
            m_file_paths = { environment_texture_directory + "syferfontein_0d_clear_4k.hdr" };
        }
    }

    void Environment::OnTick()
    {
        if (m_is_dirty)
        {
            if (m_environment_type == EnvironmentType::Cubemap)
            {
                SetFromTextureArray(m_file_paths);

            }
            else if (m_environment_type == EnvironmentType::Sphere)
            {

                SetFromTextureSphere(m_file_paths.front());
            }
            
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

        m_is_dirty = true;
    }

    const shared_ptr<RHI_Texture> Environment::GetTexture() const
    {
        return m_texture;
    }

    void Environment::SetTexture(const shared_ptr<RHI_Texture> texture)
    {
        m_texture = texture;
        Renderer::SetEnvironment(this);
    }

    void Environment::SetFromTextureArray(const vector<string>& file_paths)
    {
        if (file_paths.empty())
            return;

        SP_LOG_INFO("Loading sky box...");

        // Load all textures (sides)
        shared_ptr<RHI_Texture> texture = make_shared<RHI_TextureCube>();
        for (uint32_t slice_index = 0; static_cast<uint32_t>(file_paths.size()); slice_index++)
        {
            ResourceCache::GetImageImporter()->Load(file_paths[slice_index], slice_index, static_cast<RHI_Texture*>(texture.get()));
        }

        // Set resource file path
        texture->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "environment" + EXTENSION_TEXTURE);

        // Save file path for serialization/deserialisation
        m_file_paths = { texture->GetResourceFilePath() };

        // Pass the texture to the renderer.
        SetTexture(texture);

        SP_LOG_INFO("Sky box has been created successfully");
    }

    void Environment::SetFromTextureSphere(const string& file_path)
    {
        SP_LOG_INFO("Loading sky sphere...");

        // Create texture
        shared_ptr<RHI_Texture> texture = make_shared<RHI_Texture2D>(RHI_Texture_Srv | RHI_Texture_Mips);
        if (!texture->LoadFromFile(file_path))
        {
            SP_LOG_ERROR("Sky sphere creation failed");
        }

        // Save file path for serialization/deserialisation
        m_file_paths = { texture->GetResourceFilePath() };

        ResourceCache::Cache(texture);

        // Pass the texture to the renderer.
        SetTexture(texture);

        SP_LOG_INFO("Sky sphere has been created successfully");
    }
}
