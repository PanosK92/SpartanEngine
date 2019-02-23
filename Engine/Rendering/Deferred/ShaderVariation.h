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
#include <vector>
#include "../../Math/Vector2.h"
#include "../../Math/Matrix.h"
#include "../../RHI/RHI_Definition.h"
#include "../../RHI/RHI_Shader.h"
//===================================

namespace Directus
{
	class Material;
	class Transform;

	enum Variation_Flag : unsigned long
	{
		Variation_Albedo	= 1UL << 0,
		Variation_Roughness	= 1UL << 1,
		Variation_Metallic	= 1UL << 2,
		Variation_Normal	= 1UL << 3,
		Variation_Height	= 1UL << 4,
		Variation_Occlusion	= 1UL << 5,
		Variation_Emission	= 1UL << 6,
		Variation_Mask		= 1UL << 7
	};

	class ShaderVariation : public RHI_Shader, public std::enable_shared_from_this<ShaderVariation>
	{
	public:
		ShaderVariation(std::shared_ptr<RHI_Device> device, Context* context);
		~ShaderVariation();

		void Compile(const std::string& filePath, unsigned long shaderFlags);
		void UpdatePerObjectBuffer(Transform* transform, Material* material, const Math::Matrix& mView, const Math::Matrix mProjection);

		unsigned long GetShaderFlags()	{ return m_variationFlags; }
		bool HasAlbedoTexture()			{ return m_variationFlags & Variation_Albedo; }
		bool HasRoughnessTexture()		{ return m_variationFlags & Variation_Roughness; }
		bool HasMetallicTexture()		{ return m_variationFlags & Variation_Metallic; }
		bool HasNormalTexture()			{ return m_variationFlags & Variation_Normal; }
		bool HasHeightTexture()			{ return m_variationFlags & Variation_Height; }
		bool HasOcclusionTexture()		{ return m_variationFlags & Variation_Occlusion; }
		bool HasEmissionTexture()		{ return m_variationFlags & Variation_Emission; }
		bool HasMaskTexture()			{ return m_variationFlags & Variation_Mask; }

		std::shared_ptr<RHI_ConstantBuffer>& GetPerObjectBuffer()	{ return m_constantBuffer; }

		// Variation cache
		static std::shared_ptr<ShaderVariation> GetMatchingShader(unsigned long flags);

	private:
		void AddDefinesBasedOnMaterial();
		
		Context* m_context;
		unsigned long m_variationFlags;

		// Variation cache
		static std::vector<std::shared_ptr<ShaderVariation>> m_variations;
		
		// BUFFER
		struct PerObjectBufferType
		{
			Math::Vector4 matAlbedo;
			Math::Vector2 matTilingUV;
			Math::Vector2 matOffsetUV;
			float matRoughnessMul;
			float matMetallicMul;
			float matNormalMul;
			float matHeightMul;		
			float matShadingMode;
			Math::Vector3 padding;
			Math::Matrix mModel;
			Math::Matrix mMVP_current;
			Math::Matrix mMVP_previous;
		};
		PerObjectBufferType perObjectBufferCPU;
		std::shared_ptr<RHI_ConstantBuffer> m_constantBuffer;
	};
}