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

#pragma once

//= INCLUDES ========================
#include <memory>
#include "IComponent.h"
#include "../../RHI/RHI_Definition.h"
//===================================

namespace Spartan
{
	class Material;

	enum Skybox_Type
	{
		Skybox_Array,
		Skybox_Sphere
	};

	class SPARTAN_CLASS Skybox : public IComponent
	{
	public:
        Skybox() = default;
		Skybox(Context* context, Entity* entity);
		~Skybox();

		//= IComponent ==============
		void OnInitialize() override;
		//===========================

		const std::shared_ptr<RHI_Texture>& GetTexture()	{ return m_texture; }
		std::weak_ptr<Material> GetMaterial()				{ return m_material; }

	private:

		void CreateFromArray(const std::vector<std::string>& texturePaths);
		void CreateFromSphere(const std::string& texturePath);

		std::vector<std::string> m_texture_paths;
		std::shared_ptr<RHI_Texture> m_texture;
		std::shared_ptr<Material> m_material;
		Skybox_Type m_environment_type;
	};
}
