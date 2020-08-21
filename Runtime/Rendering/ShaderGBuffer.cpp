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

//= INCLUDES =========================
#include "Spartan.h"
#include "ShaderGBuffer.h"
#include "Material.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    unordered_map<uint16_t, shared_ptr<ShaderGBuffer>> ShaderGBuffer::m_variations;

    ShaderGBuffer::ShaderGBuffer(Context* context, const uint16_t flags /*= 0*/) : RHI_Shader(context)
    {
        m_flags = flags;
    }

    const ShaderGBuffer* ShaderGBuffer::GenerateVariation(Context* context, const uint16_t flags)
    {
        // Return existing shader, if it's already compiled
        if (m_variations.find(flags) != m_variations.end())
            return m_variations.at(flags).get();

        // Compile new shader
        return Compile(context, flags);
    }

    ShaderGBuffer* ShaderGBuffer::Compile(Context* context, const uint16_t flags)
    {
        // Shader source file path
        const string file_path = context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Shaders) + "/GBuffer.hlsl";

        // Make new
        shared_ptr<ShaderGBuffer> shader = make_shared<ShaderGBuffer>(context, flags);

        // Add defines based on flag properties
        shader->AddDefine("ALBEDO_MAP",        (flags & Material_Color)      ? "1" : "0");
        shader->AddDefine("ROUGHNESS_MAP",  (flags & Material_Roughness)  ? "1" : "0");
        shader->AddDefine("METALLIC_MAP",   (flags & Material_Metallic)   ? "1" : "0");
        shader->AddDefine("NORMAL_MAP",     (flags & Material_Normal)     ? "1" : "0");
        shader->AddDefine("HEIGHT_MAP",     (flags & Material_Height)     ? "1" : "0");
        shader->AddDefine("OCCLUSION_MAP",  (flags & Material_Occlusion)  ? "1" : "0");
        shader->AddDefine("EMISSION_MAP",   (flags & Material_Emission)   ? "1" : "0");
        shader->AddDefine("MASK_MAP",       (flags & Material_Mask)       ? "1" : "0");

        // Compile
        shader->CompileAsync(RHI_Shader_Pixel, file_path);

        // Save
        m_variations[flags] = shader;

        return shader.get();
    }
}
