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

#pragma once

//= INCLUDES ====================
#include "Component.h"
#include "../Graphics/Material.h"
//===============================

namespace Directus
{
	class Light;

	class DLL_API MeshRenderer : public Component
	{
	public:
		MeshRenderer();
		~MeshRenderer();

		//= ICOMPONENT =============================
		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();

		//= MISC ===================================
		void Render(unsigned int indexCount);

		//= PROPERTIES =============================
		void SetCastShadows(bool castShadows) { m_castShadows = castShadows; }
		bool GetCastShadows() { return m_castShadows; }
		void SetReceiveShadows(bool receiveShadows) { m_receiveShadows = receiveShadows; }
		bool GetReceiveShadows() { return m_receiveShadows; }

		//= MATERIAL ===============================
		// Sets a material from memory
		void SetMaterialFromMemory(std::weak_ptr<Material> material);

		// Loads a material and the sets it
		std::weak_ptr<Material> SetMaterialFromFile(const std::string& filePath);

		// Sets a default material (basic, skybox)
		void SetMaterialByType(MaterialType type);

		// Sets a material based on it's ID
		std::weak_ptr<Material> SetMaterialByID(const std::string& ID);

		std::weak_ptr<Material>& GetMaterial() { return  m_material; }
		bool HasMaterial() { return GetMaterial().expired() ? false : true; }
		std::string GetMaterialName() { return !GetMaterial().expired() ? GetMaterial()._Get()->GetResourceName() : NOT_ASSIGNED; }
		MaterialType GetMaterialType() { return m_materialType; }

	private:
		std::string GetGameObjectName();

		std::weak_ptr<Material> m_material;
		bool m_castShadows;
		bool m_receiveShadows;
		MaterialType m_materialType;
	};
}
