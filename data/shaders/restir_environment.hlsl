/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_ENVIRONMENT
#define SPARTAN_RESTIR_ENVIRONMENT

// Match the 4096x2048 sky's radiance mip 2. Each cell has exact spherical area.
static const uint RESTIR_ENV_WIDTH = 1024u;
static const uint RESTIR_ENV_HEIGHT = 512u;
static const uint RESTIR_ENV_CELLS = RESTIR_ENV_WIDTH * RESTIR_ENV_HEIGHT;
// Cells: (mass, inclusive row CDF). Tail: (row mass, inclusive marginal CDF).
RWStructuredBuffer<float2> restir_environment_cdf : register(u75);

float restir_environment_cell_area(uint row)
{
    return (PI2 / float(RESTIR_ENV_WIDTH)) *
        (cos(PI * float(row) / float(RESTIR_ENV_HEIGHT)) -
         cos(PI * float(row + 1u) / float(RESTIR_ENV_HEIGHT)));
}

float3 restir_environment_direction(float phi, float y)
{
    float radius = sqrt(max(1.0f - y * y, 0.0f));
    return float3(cos(phi) * radius, y, sin(phi) * radius);
}

float sky_nee_pdf_at(float3 dir, float3 shading_normal)
{
    float total = restir_environment_cdf[RESTIR_ENV_CELLS + RESTIR_ENV_HEIGHT - 1u].y;
    if (!(total > 0.0f)) return 1.0f / (4.0f * PI);
    float2 uv = direction_sphere_uv(dir);
    uint2 cell = min(uint2(frac(uv.x) * RESTIR_ENV_WIDTH, saturate(uv.y) * RESTIR_ENV_HEIGHT),
        uint2(RESTIR_ENV_WIDTH - 1u, RESTIR_ENV_HEIGHT - 1u));
    float2 marginal = restir_environment_cdf[RESTIR_ENV_CELLS + cell.y];
    float previous_row = cell.y > 0u ? restir_environment_cdf[RESTIR_ENV_CELLS + cell.y - 1u].y : 0.0f;
    float end = restir_environment_cdf[cell.y * RESTIR_ENV_WIDTH + cell.x].y;
    float previous_cell = cell.x > 0u ? restir_environment_cdf[cell.y * RESTIR_ENV_WIDTH + cell.x - 1u].y : 0.0f;
    // Use the stored CDF interval widths, including their floating-point rounding.
    return max(marginal.y - previous_row, 0.0f) / total *
        (max(end - previous_cell, 0.0f) / max(marginal.x, 1e-30f)) / restir_environment_cell_area(cell.y);
}

// Inverting both CDFs leaves a uniform residual in each selected interval.
// Reuse those residuals for phi/cos(theta), retaining the two replay dimensions.
float3 sample_restir_environment(float2 xi, out float pdf)
{
    float total = restir_environment_cdf[RESTIR_ENV_CELLS + RESTIR_ENV_HEIGHT - 1u].y;
    if (!(total > 0.0f))
    {
        pdf = 1.0f / (4.0f * PI);
        return restir_environment_direction((xi.y - 0.5f) * PI2, 1.0f - 2.0f * xi.x);
    }
    float target = min(xi.x * total, asfloat(asuint(total) - 1u));
    uint lo = 0u, hi = RESTIR_ENV_HEIGHT - 1u;
    while (lo < hi)
    {
        uint mid = (lo + hi) / 2u;
        if (restir_environment_cdf[RESTIR_ENV_CELLS + mid].y <= target) lo = mid + 1u;
        else hi = mid;
    }
    uint row = lo;
    float2 marginal = restir_environment_cdf[RESTIR_ENV_CELLS + row];
    float previous = row > 0u ? restir_environment_cdf[RESTIR_ENV_CELLS + row - 1u].y : 0.0f;
    float row_mass = marginal.y - previous;
    float row_xi = saturate((target - previous) / max(row_mass, 1e-30f));
    target = min(xi.y * marginal.x, asfloat(asuint(marginal.x) - 1u));
    lo = 0u; hi = RESTIR_ENV_WIDTH - 1u;
    while (lo < hi)
    {
        uint mid = (lo + hi) / 2u;
        if (restir_environment_cdf[row * RESTIR_ENV_WIDTH + mid].y <= target) lo = mid + 1u;
        else hi = mid;
    }
    uint column = lo;
    float2 cell = restir_environment_cdf[row * RESTIR_ENV_WIDTH + column];
    previous = column > 0u ? restir_environment_cdf[row * RESTIR_ENV_WIDTH + column - 1u].y : 0.0f;
    float cell_mass = cell.y - previous;
    float column_xi = saturate((target - previous) / max(cell_mass, 1e-30f));
    float phi = ((float(column) + column_xi) / float(RESTIR_ENV_WIDTH) - 0.5f) * PI2;
    float y = lerp(cos(PI * float(row) / float(RESTIR_ENV_HEIGHT)),
        cos(PI * float(row + 1u) / float(RESTIR_ENV_HEIGHT)), row_xi);
    pdf = (row_mass / total) * (cell_mass / marginal.x) / restir_environment_cell_area(row);
    return restir_environment_direction(phi, y);
}
#endif
