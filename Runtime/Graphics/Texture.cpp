/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= LINKING ==========================
// Required by DDSTextureLoader when using Windows 10 SDK
//#pragma comment(lib, "WindowsApp.lib")
//====================================


//= INCLUDES =====================================
#include "Texture.h"
#include "../Logging/Log.h"
#include "../Core/Helper.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/DDSTextureImporter.h"
#include "../Resource/ResourceManager.h"
#include "D3D11/D3D11Texture.h"
#include "../Resource/TextureInfo.h"
//================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	static const char* textureTypeChar[] =
	{
		"Unknown",
		"Albedo",
		"Roughness",
		"Metallic",
		"Normal",
		"Height",
		"Occlusion",
		"Emission",
		"Mask",
		"CubeMap",
	};

	Texture::Texture(Context* context)
	{
		//= RESOURCE INTERFACE ==============
		InitializeResource(Resource_Texture);
		//===================================

		// Texture
		m_context = context;
		m_textureInfo = make_unique<TextureInfo>();
		m_textureInfo->isUsingMipmaps = true;
		m_textureAPI = make_unique<D3D11Texture>(m_context->GetSubsystem<Graphics>());
		m_isDirty = false;
	}

	Texture::~Texture()
	{

	}

	//= RESOURCE INTERFACE =====================================================================
	bool Texture::SaveToFile(const string& filePath)
	{
		if (!m_textureInfo || !m_isDirty)
			return false;

		m_isDirty = false;
		m_textureInfo->Serialize(filePath);
		m_textureInfo->Clear();

		// Serialize data in another thread so it doesn't stall the engine
		/*m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
		{
			m_textureInfo->Serialize(filePath);
			m_textureInfo->Clear();
		});*/

		return true;
	}

	bool Texture::LoadFromFile(const string& filePath)
	{
		bool loaded;

		if (FileSystem::GetExtensionFromFilePath(filePath) == TEXTURE_EXTENSION) // engine format
		{
			loaded = m_textureInfo->Deserialize(filePath);
		}
		else // foreign format
		{
			loaded = LoadFromForeignFormat(filePath);
		}

		if (!loaded)
		{
			LOG_ERROR("Texture: Failed to load \"" + filePath + "\".");
			return false;
		}

		CreateShaderResource(m_textureInfo.get());

		m_isDirty = true;
		return true;
	}
	//===========================================================================================

	//= PROPERTIES ==============================================================================
	unsigned int Texture::GetWidth()
	{
		return m_textureInfo->width;
	}

	void Texture::SetWidth(unsigned int width)
	{
		m_textureInfo->width = width;
		m_isDirty = true;
	}

	unsigned Texture::GetHeight()
	{
		return m_textureInfo->height;
	}

	void Texture::SetHeight(unsigned int height)
	{
		m_textureInfo->height = height;
		m_isDirty = true;
	}

	TextureType Texture::GetTextureType()
	{
		return m_textureInfo->type;
	}

	void Texture::SetTextureType(TextureType type)
	{
		m_textureInfo->type = type;

		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		if (m_textureInfo->type == TextureType_Height && !GetGrayscale())
		{
			m_textureInfo->type = TextureType_Normal;
		}

		if (m_textureInfo->type == TextureType_Normal && GetGrayscale())
		{
			m_textureInfo->type = TextureType_Height;
		}

		m_isDirty = true;
	}

	bool Texture::GetGrayscale()
	{
		return m_textureInfo->isGrayscale;
	}

	void Texture::SetGrayscale(bool grayscale)
	{
		m_textureInfo->isGrayscale = grayscale;
		m_isDirty = true;
	}

	bool Texture::GetTransparency()
	{
		return m_textureInfo->isTransparent;
	}

	void Texture::SetTransparency(bool transparency)
	{
		m_textureInfo->isTransparent = transparency;
		m_isDirty = true;
	}

	void Texture::EnableMimaps(bool enable)
	{
		m_textureInfo->isUsingMipmaps = enable;
		m_isDirty = true;
	}
	//==========================================================================================

	void** Texture::GetShaderResource()
	{
		if (!m_textureAPI)
			return nullptr;

		return (void**)m_textureAPI->GetShaderResourceView();
	}

	bool Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, vector<unsigned char> rgba, TextureFormat format)
	{
		return m_textureAPI->Create(width, height, channels, rgba, (DXGI_FORMAT)ToAPIFormat(format));
	}

	bool Texture::CreateShaderResource(TextureInfo* texInfo)
	{
		if (!m_textureAPI)
		{
			LOG_ERROR("Texture: Failed to create shader resource. API texture not initialized.");
			return false;
		}

		if (!texInfo->isUsingMipmaps)
		{
			if (!m_textureAPI->Create(texInfo->width, texInfo->height, texInfo->channels, texInfo->rgba, (DXGI_FORMAT)ToAPIFormat(m_format)))
			{
				LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}
		else
		{
			if (!m_textureAPI->CreateWithMipmaps(texInfo->width, texInfo->height, texInfo->channels, texInfo->rgba_mimaps, (DXGI_FORMAT)ToAPIFormat(m_format)))
			{
				LOG_ERROR("Texture: Failed to create shader resource with mipmaps for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}

		return true;
	}

	bool Texture::LoadFromForeignFormat(const string& filePath)
	{
		// Load DDS (too bored to implement dds cubemap support in the ImageImporter)
		if (FileSystem::GetExtensionFromFilePath(filePath) == ".dds")
		{
			auto graphicsDevice = m_context->GetSubsystem<Graphics>()->GetDevice();
			if (!graphicsDevice)
				return false;

			ID3D11ShaderResourceView* ddsTex = nullptr;
			wstring widestr = wstring(filePath.begin(), filePath.end());
			auto hresult = DirectX::CreateDDSTextureFromFile(graphicsDevice, widestr.c_str(), nullptr, &ddsTex);
			if (FAILED(hresult))
			{
				return false;
			}

			m_textureAPI->SetShaderResourceView(ddsTex);
			m_isDirty = true;
			return true;
		}

		// Load texture
		weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();	
		if (!imageImp._Get()->Load(filePath, *m_textureInfo.get()))
		{
			return false;
		}

		// Extract any metadata we can from the ImageImporter
		SetResourceFilePath(filePath);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		m_isDirty = true;
		return true;
	}

	TextureType Texture::TextureTypeFromString(const string& type)
	{
		if (type == "Albedo") return TextureType_Albedo;
		if (type == "Roughness") return TextureType_Roughness;
		if (type == "Metallic") return TextureType_Metallic;
		if (type == "Normal") return TextureType_Normal;
		if (type == "Height") return TextureType_Height;
		if (type == "Occlusion") return TextureType_Occlusion;
		if (type == "Emission") return TextureType_Emission;
		if (type == "Mask") return TextureType_Mask;
		if (type == "CubeMap") return TextureType_CubeMap;

		return TextureType_Unknown;
	}

	int Texture::ToAPIFormat(TextureFormat format)
	{
		if (format == RGBA_8_UNORM) return DXGI_FORMAT_R8G8B8A8_UNORM;
		if (format == RGBA_32_FLOAT) return DXGI_FORMAT_R32G32B32_FLOAT;
		if (format == RGBA_16_FLOAT) return DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (format == R_8_UNORM) return DXGI_FORMAT_R8_UNORM;

		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}
