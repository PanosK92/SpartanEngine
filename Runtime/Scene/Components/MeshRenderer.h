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

#pragma once

//= INCLUDES ==========
#include "IComponent.h"
//=====================

namespace Directus
{
	class Light;
	class Material;

	class ENGINE_CLASS MeshRenderer : public IComponent
	{
	public:
		MeshRenderer(Context* context, GameObject* gameObject, Transform* transform);
		~MeshRenderer();

		//= ICOMPONENT ===============================
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= RENDERING =======================
		void Render(unsigned int indexCount);
		//===================================

		//= PROPERTIES ===================================================================
		void SetCastShadows(bool castShadows) { m_castShadows = castShadows; }
		bool GetCastShadows() { return m_castShadows; }
		void SetReceiveShadows(bool receiveShadows) { m_receiveShadows = receiveShadows; }
		bool GetReceiveShadows() { return m_receiveShadows; }
		//================================================================================

		//= MATERIAL ==================================================================================
		// Sets a material from memory (adds it to the resource cache by default)
		void SetMaterialFromMemory(const std::weak_ptr<Material>& materialWeak, bool autoCache = true);

		// Loads a material and the sets it
		std::weak_ptr<Material> SetMaterialFromFile(const std::string& filePath);

		void UseStandardMaterial();

		std::weak_ptr<Material> GetMaterial_RefWeak() { return m_materialRefWeak; }
		Material* GetMaterial_Ref() { return m_materialRef; }
		bool HasMaterial() { return !m_materialRefWeak.expired(); }
		std::string GetMaterialName();
		//=============================================================================================

	private:
		std::weak_ptr<Material> m_materialRefWeak;
		Material* m_materialRef;
		bool m_castShadows;
		bool m_receiveShadows;
		bool m_usingStandardMaterial;
	};
}
