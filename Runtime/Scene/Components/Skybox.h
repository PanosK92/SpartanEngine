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

//= INCLUDES ========================
#include <memory>
#include "IComponent.h"
#include "../../RHI/RHI_Definition.h"
//===================================

namespace Directus
{
	class Material;

	class ENGINE_CLASS Skybox : public IComponent
	{
	public:
		Skybox(Context* context, Actor* actor, Transform* transform);
		~Skybox();

		//= IComponent ==============
		void OnInitialize() override;
		void OnTick() override;
		//===========================

		const std::shared_ptr<RHI_Texture>& GetTexture()	{ return m_cubemapTexture; }
		std::weak_ptr<Material> GetMaterial()				{ return m_matSkybox;}

	private:
		// Cubemap sides
		std::string m_filePath_back;
		std::string m_filePath_down;
		std::string m_filePath_front;
		std::string m_filePath_left;
		std::string m_filePath_right;
		std::string m_filepath_up;
		unsigned int m_size;

		// Cubemap texture
		std::shared_ptr<RHI_Texture> m_cubemapTexture;
		Texture_Format m_format;

		// Material
		std::shared_ptr<Material> m_matSkybox;
	};
}