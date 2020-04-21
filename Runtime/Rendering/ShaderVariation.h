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

#pragma once

//= INCLUDES =====================
#include <memory>
#include <vector>
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Shader.h"
//================================

namespace Spartan
{
	class ShaderVariation : public RHI_Shader, public std::enable_shared_from_this<ShaderVariation>
	{
	public:
		ShaderVariation(const std::shared_ptr<RHI_Device>& rhi_device, Context* context);
		~ShaderVariation() = default;

		void Compile(const std::string& file_path, const uint16_t shader_flags);
        uint16_t GetFlags() const { return m_flags; }
		static const std::shared_ptr<ShaderVariation>& GetMatchingShader(const uint16_t flags);
        static const auto& GetVariations() { return m_variations; }

	private:
		void AddDefinesBasedOnMaterial();

        uint16_t m_flags = 0;
		static std::vector<std::shared_ptr<ShaderVariation>> m_variations;
	};
}
