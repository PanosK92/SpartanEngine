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

struct Vertex_Pos
{
    float4 position : POSITION0;
};

struct Vertex_PosUv
{
    float4 position : POSITION0;
    float2 uv       : TEXCOORD0;
};

struct Vertex_PosColor
{
    float4 position : POSITION0;
    float4 color    : COLOR0;
};

struct Vertex_PosUvNorTan
{
    float4 position     : POSITION0;
    float2 uv           : TEXCOORD0;
    float3 normal       : NORMAL0;
    float3 tangent      : TANGENT0;
};

struct Vertex_Pos2dUvColor
{
    float2 position     : POSITION0;
    float2 uv           : TEXCOORD0;
    float4 color        : COLOR0;
};

struct Pixel_Pos
{
    float4 position : SV_POSITION;
};

struct Pixel_PosUv
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

struct Pixel_PosColor
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};