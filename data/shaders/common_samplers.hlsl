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

// note: clamp to border uses black transparent RGBA(0, 0, 0, 0)

// comparison
static const uint sampler_compare_depth = 0;

// regular
static const uint sampler_point_clamp_edge      = 0;
static const uint sampler_point_clamp_border    = 1;
static const uint sampler_point_wrap            = 2;
static const uint sampler_bilinear_clamp        = 3;
static const uint sampler_bilinear_clamp_border = 4;
static const uint sampler_bilinear_wrap         = 5;
static const uint sampler_trilinear_clamp       = 6;
static const uint sampler_anisotropic_wrap      = 7;

// samplers
SamplerComparisonState samplers_comparison[] : register(s0, space2);
SamplerState samplers[]                      : register(s1, space3);

#define GET_SAMPLER(index_sampler) samplers[index_sampler]
