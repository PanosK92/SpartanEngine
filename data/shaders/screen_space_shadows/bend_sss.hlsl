/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============
#include "../common.hlsl"
//=======================

#define WAVE_SIZE           64
#define SAMPLE_COUNT        256 // shadow samples per-pixel, determines overall cost, default: 60
#define HARD_SHADOW_SAMPLES 0   // initial shadow samples that will produce a hard shadow, and not perform sample-averaging, defauult: 4
#define FADE_OUT_SAMPLES    64  // samples that will fade out at the end of the shadow (for a minor cost), default : 8

#include "bend_sss_gpu.hlsl"

[numthreads(WAVE_SIZE, 1, 1)]
void main_cs
(
    uint3 DTid      : SV_DispatchThreadID,
    uint3 Gid       : SV_GroupID,
    uint3 GTid      : SV_GroupThreadID,
    uint groupIndex : SV_GroupIndex
)
{
    DispatchParameters in_parameters;
    in_parameters.SetDefaults();
    in_parameters.BilinearThreshold          = 0.04f;                                // recommended starting value: 0.02 (2%)
    in_parameters.BilinearSamplingOffsetMode = true;
    in_parameters.IgnoreEdgePixels           = true;
    in_parameters.SurfaceThickness           = 0.005f;                               // recommended starting value: 0.005
    in_parameters.ShadowContrast             = 4;                                    // recommended starting value: 2 or 4
    in_parameters.LightCoordinate            = pass_float4(pass_bend_sss::light_coordinate);       // values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
    in_parameters.WaveOffset                 = pass_float2(pass_bend_sss::wave_offset);            // values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()
    in_parameters.NearDepthValue             = pass_float(pass_bend_sss::near_depth);              // set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).
    in_parameters.FarDepthValue              = pass_float(pass_bend_sss::far_depth);               // set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
    in_parameters.ArraySliceIndex            = pass_uint(pass_bend_sss::array_slice);
    in_parameters.InvDepthTextureSize        = pass_float2(pass_bend_sss::inv_depth_texture_size); // inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
    in_parameters.DepthTexture               = tex;
    in_parameters.OutputTexture              = tex_uav_sss;
    in_parameters.PointBorderSampler         = samplers[sampler_point_clamp_border]; // a point sampler, with Wrap Mode set to Clamp-To-Border-Color (D3D12_TEXTURE_ADDRESS_MODE_BORDER), and Border Color set to "FarDepthValue" (typically zero), or some other far-depth value out of DepthBounds.
    in_parameters.DebugOutputEdgeMask        = false;                                // use this to visualize edges, for tuning the 'BilinearThreshold' value.
    in_parameters.DebugOutputThreadIndex     = false;                                // debug output to visualize layout of compute threads
    in_parameters.DebugOutputWaveIndex       = false;                                // debug output to visualize layout of compute wavefronts, useful to sanity check the Light Coordinate is being computed correctly.
    
    WriteScreenSpaceShadow(in_parameters, Gid, GTid.x);
}
