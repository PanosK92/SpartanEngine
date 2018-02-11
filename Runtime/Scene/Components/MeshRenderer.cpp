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

//= INCLUDES ==============================================
#include "MeshRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../../Logging/Log.h"
#include "../../IO/FileStream.h"
#include "../../Graphics/Material.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Resource/ResourceManager.h"
#include "../../Graphics/DeferredShaders/ShaderVariation.h"
//=========================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshRenderer::MeshRenderer()
	{
		m_castShadows			= true;
		m_receiveShadows		= true;
		m_usingStandardMaterial = false;
	}

	MeshRenderer::~MeshRenderer()
	{

	}

	//= ICOMPONENT ===============================================================
	void MeshRenderer::Serialize(FileStream* stream)
	{
		stream->Write(m_usingStandardMaterial);
		stream->Write(!m_material.expired() ? m_material.lock()->GetResourceName() : NOT_ASSIGNED);
		stream->Write(m_castShadows);
		stream->Write(m_receiveShadows);
	}

	void MeshRenderer::Deserialize(FileStream* stream)
	{
		std::string materialName;

		stream->Read(&m_usingStandardMaterial);
		stream->Read(&materialName);
		stream->Read(&m_castShadows);
		stream->Read(&m_receiveShadows);

		if (m_usingStandardMaterial)
		{
			UseStandardMaterial();
		}
		else
		{
			m_material = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Material>(materialName);
		}
	}
	//==============================================================================

	//= MISC =======================================================================
	void MeshRenderer::Render(unsigned int indexCount)
	{
		// Check if a material exists
		if (m_material.expired()) 
		{
			LOG_WARNING("MeshRenderer: \"" + GetGameObjectName() + "\" has no material. It can't be rendered.");
			return;
		}
		// Check if the material has a shader
		if (!m_material.lock()->HasShader()) 
		{
			LOG_WARNING("MeshRenderer: \"" + GetGameObjectName() + "\" has a material but not a shader associated with it. It can't be rendered.");
			return;
		}

		// Get it's shader and render
		m_material.lock()->GetShader().lock()->Render(indexCount);
	}

	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void MeshRenderer::SetMaterialFromMemory(const weak_ptr<Material>& materialWeak, bool autoCache /* true */)
	{
		// Validate material
		auto material = materialWeak.lock();
		if (!material)
		{
			LOG_WARNING("MeshRenderer::SetMaterialFromMemory(): Provided material is null, can't execute function");
			return;
		}

		if (autoCache) // Cache it
		{
			if (auto cachedMat = material->Cache<Material>().lock())
			{
				m_material = cachedMat;
				m_material.lock()->SaveToFile(material->GetResourceFilePath());
			}
			m_usingStandardMaterial = false;
		}
		else
		{
			// Anything that's we don't cache is considered a standard material.
			// A standard material's lifetime is entirely up to the caller.
			m_material = material;
			m_usingStandardMaterial = true;
		}
	}

	weak_ptr<Material> MeshRenderer::SetMaterialFromFile(const string& filePath)
	{
		// Load the material
		auto material = make_shared<Material>(GetContext());
		if (!material->LoadFromFile(filePath))
		{
			LOG_WARNING("MeshRenderer::SetMaterialFromFile(): Failed to load material from \"" + filePath + "\"");
			return weak_ptr<Material>();
		}

		// Set it as the current material
		SetMaterialFromMemory(material);

		// Return it
		return GetMaterial();
	}

	void MeshRenderer::UseStandardMaterial()
	{
		auto projectStandardAssetDir = GetContext()->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);
		auto materialStandard = make_shared<Material>(GetContext());
		materialStandard->SetResourceName("Standard");
		materialStandard->SetCullMode(CullBack);
		materialStandard->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		materialStandard->SetIsEditable(false);
		SetMaterialFromMemory(materialStandard->Cache<Material>(), false);
	}

	//==============================================================================

	string MeshRenderer::GetMaterialName()
	{
		return !GetMaterial().expired() ? GetMaterial().lock()->GetResourceName() : NOT_ASSIGNED;
	}
}
