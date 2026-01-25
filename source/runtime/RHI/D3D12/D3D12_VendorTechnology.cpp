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

    void RHI_VendorTechnology::FSR3_GenerateJitterSample(float* x, float* y)
    {

    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {

    }

    void RHI_VendorTechnology::ResetHistory()
    {
        
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {

    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_CommandList* cmd_list,
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
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {

    }

    void RHI_VendorTechnology::NRD_Initialize(uint32_t width, uint32_t height)
    {

    }

    void RHI_VendorTechnology::NRD_Shutdown()
    {

    }

    void RHI_VendorTechnology::NRD_Resize(uint32_t width, uint32_t height)
    {

    }

    void RHI_VendorTechnology::NRD_Denoise(
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_noisy,
        RHI_Texture* tex_output,
        const Matrix& view_matrix,
        const Matrix& projection_matrix,
        const Matrix& view_matrix_prev,
        const Matrix& projection_matrix_prev,
        float jitter_x,
        float jitter_y,
        float jitter_prev_x,
        float jitter_prev_y,
        float time_delta_ms,
        uint32_t frame_index
    )
    {

    }

    bool RHI_VendorTechnology::NRD_IsAvailable()
    {
        return false;
    }
}
