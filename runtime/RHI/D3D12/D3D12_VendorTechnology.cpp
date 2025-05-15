/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =============================
#include "pch.h"
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../../World/Components/Camera.h"
//========================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    void RHI_VendorTechnology::Initialize()
    {

    }

    void RHI_VendorTechnology::Shutdown()
    {

    }

    void RHI_VendorTechnology::FSR3_ResetHistory()
    {

    }

    void RHI_VendorTechnology::FSR3_GenerateJitterSample(float* x, float* y)
    {

    }

    void RHI_VendorTechnology::Resize(const Vector2& resolution_render, const Vector2& resolution_output)
    {

    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame)
    {

    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {

    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_CommandList* cmd_list,
        const bool reset_history,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
   
    }

    void RHI_VendorTechnology::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {

    }

    void RHI_VendorTechnology::SSSR_Dispatch(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        RHI_Texture* tex_brdf,
        RHI_Texture* tex_output
    )
    {

    }

    void RHI_VendorTechnology::BrixelizerGI_Update(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
        Cb_Frame* cb_frame,
        const vector<shared_ptr<Entity>>& entities,
        RHI_Texture* tex_debug
    )
    {

    }

    void RHI_VendorTechnology::BrixelizerGI_Dispatch(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        array<RHI_Texture*, 8>& tex_noise,
        RHI_Texture* tex_diffuse_gi,
        RHI_Texture* tex_specular_gi,
        RHI_Texture* tex_debug
    )
    {

    }

    void RHI_VendorTechnology::BrixelizerGI_SetResolutionPercentage(const float resolution_percentage)
    {

    }

    void RHI_VendorTechnology::Breadcrumbs_RegisterCommandList(RHI_CommandList* cmd_list, const RHI_Queue* queue, const char* name)
    {

    }

    void RHI_VendorTechnology::Breadcrumbs_SetPipelineState(RHI_CommandList* cmd_list, RHI_Pipeline* pipeline)
    {

    }

    void RHI_VendorTechnology::Breadcrumbs_MarkerBegin(RHI_CommandList* cmd_list, const AMD_FFX_Marker marker, const char* name)
    {

    }

    void RHI_VendorTechnology::Breadcrumbs_MarkerEnd(RHI_CommandList* cmd_list)
    {

    }

    void RHI_VendorTechnology::Breadcrumbs_OnDeviceRemoved()
    {

    }
}
