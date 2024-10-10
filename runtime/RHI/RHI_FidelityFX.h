/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===============
#include "../Math/Vector2.h"
#include "RHI_Definitions.h"
//==========================

namespace Spartan
{
    class Camera;
    struct Cb_Frame;

    class RHI_FidelityFX
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Resize(const Math::Vector2& resolution_render, const Math::Vector2& resolution_output);
        static void Tick(Cb_Frame* cb_frame);

        // fsr 3
        static void FSR3_ResetHistory();
        static void FSR3_GenerateJitterSample(float* x, float* y);
        static void FSR3_Dispatch(
            RHI_CommandList* cmd_list,
            Camera* camera,
            const float delta_time,
            const float sharpness,
            const float exposure,
            const float resolution_scale,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_output
        );

        // sssr
        static void SSSR_Dispatch(
            RHI_CommandList* cmd_list,
            const float resolution_scale,
            RHI_Texture* tex_color,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_motion_vectors,
            RHI_Texture* tex_normal,
            RHI_Texture* tex_material,
            RHI_Texture* tex_brdf,
            RHI_Texture* tex_skybox,
            RHI_Texture* tex_output
        );

        // brixelizer gi
        static void BrixelizerGI_Update(
            RHI_CommandList* cmd_list,
            Cb_Frame* cb_frame,
            std::vector<std::shared_ptr<Entity>>& entities,
            int64_t index_start,
            int64_t index_end,
            RHI_Texture* tex_debug
        );
        static void BrixelizerGI_Dispatch(
            RHI_CommandList* cmd_list,
            Cb_Frame* cb_frame,
            RHI_Texture* tex_frame,
            RHI_Texture* tex_depth,
            RHI_Texture* tex_velocity,
            RHI_Texture* tex_normal,
            RHI_Texture* tex_material,
            RHI_Texture* tex_skybox,
            std::array< RHI_Texture*, 8>& tex_noise,
            RHI_Texture* tex_diffuse_gi,
            RHI_Texture* tex_specular_gi,
            RHI_Texture* tex_debug
        );

        // breadcrumbs
        static void Breadcrumbs_RegisterCommandList(RHI_CommandList* cmd_list, const RHI_Queue* queue, const char* name);
        static void Breadcrumbs_RegisterPipeline(RHI_Pipeline* pipeline);
        static void Breadcrumbs_SetPipelineState(RHI_CommandList* cmd_list, RHI_Pipeline* pipeline);
        static void Breadcrumbs_MarkerBegin(RHI_CommandList* cmd_list, const char* name);
        static void Breadcrumbs_MarkerEnd(RHI_CommandList* cmd_list);
        static void Breadcrumbs_OnDeviceRemoved(void* data);

    private:
        static void DestroySizeDependentContexts();
    };
}
