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
#include "ShaderLight.h"
#include "Renderer.h"
#include "../World/Components/Light.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    unordered_map<uint16_t, shared_ptr<ShaderLight>> ShaderLight::m_variations;

    ShaderLight::ShaderLight(Context* context, const uint16_t flags /*= 0*/) : RHI_Shader(context)
    {
        m_flags = flags;
    }

    ShaderLight* ShaderLight::GetVariation(Context* context, const Light* light, const uint64_t renderer_flags, const bool is_transparent_pass)
    {
        // Compute flags
        uint16_t flags = 0;
        flags |= is_transparent_pass                                                                        ? Shader_Light_Transparent              : flags;
        flags |= light->GetLightType() == LightType::Directional                                            ? Shader_Light_Directional              : flags;
        flags |= light->GetLightType() == LightType::Point                                                  ? Shader_Light_Point                    : flags;
        flags |= light->GetLightType() == LightType::Spot                                                   ? Shader_Light_Spot                     : flags;
        flags |= light->GetShadowsEnabled()                                                                 ? Shader_Light_Shadows                  : flags;
        flags |= (light->GetShadowsScreenSpaceEnabled() && (renderer_flags & Render_ScreenSpaceShadows))    ? Shader_Light_ShadowsScreenSpace       : flags;
        flags |= light->GetShadowsTransparentEnabled()                                                      ? Shader_Light_ShadowsTransparent       : flags;
        flags |= (light->GetVolumetricEnabled() && (renderer_flags & Render_VolumetricLighting))            ? Shader_Light_Volumetric               : flags;
        flags |= (renderer_flags & Render_ScreenSpaceReflections)                                           ? Shader_Light_ScreenSpaceReflections   : flags;

        // Return existing shader, if it's already compiled
        if (m_variations.find(flags) != m_variations.end())
            return m_variations.at(flags).get();

        // Compile new shader
        return _Compile(context, flags);
    }

    ShaderLight* ShaderLight::_Compile(Context* context, const uint16_t flags)
    {
        // Shader source file path
        const string file_path = context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Shaders) + "/Light.hlsl";

        // Make new
        shared_ptr<ShaderLight> shader = make_shared<ShaderLight>(context, flags);

        // Add defines based on flag properties
        shader->AddDefine("TRANSPARENT",                (flags & Shader_Light_Transparent)              ? "1" : "0");
        shader->AddDefine("DIRECTIONAL",                (flags & Shader_Light_Directional)              ? "1" : "0");
        shader->AddDefine("POINT",                      (flags & Shader_Light_Point)                    ? "1" : "0");
        shader->AddDefine("SPOT",                       (flags & Shader_Light_Spot)                     ? "1" : "0");
        shader->AddDefine("SHADOWS",                    (flags & Shader_Light_Shadows)                  ? "1" : "0");
        shader->AddDefine("SHADOWS_SCREEN_SPACE",       (flags & Shader_Light_ShadowsScreenSpace)       ? "1" : "0");
        shader->AddDefine("SHADOWS_TRANSPARENT",        (flags & Shader_Light_ShadowsTransparent)       ? "1" : "0");
        shader->AddDefine("VOLUMETRIC",                 (flags & Shader_Light_Volumetric)               ? "1" : "0");
        shader->AddDefine("SCREEN_SPACE_REFLECTIONS",   (flags & Shader_Light_ScreenSpaceReflections)   ? "1" : "0");

        // Compile
        shader->CompileAsync(RHI_Shader_Compute, file_path);

        // Save
        m_variations[flags] = shader;

        return shader.get();
    }
}
