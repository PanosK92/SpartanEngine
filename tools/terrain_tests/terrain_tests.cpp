/*
Copyright(c) 2015-2026 Panos Karabelas

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

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "../../source/world/TerrainPlacement.h"
#include "../../source/world/TerrainSystem.h"
#include "../../source/rendering/Instance.h"
#include "../../source/rendering/Color.h"
#include "../../data/shaders/shared_buffers.h"

using namespace spartan;
using namespace spartan::math;
using namespace spartan::terrain_placement;

static bool close(float a, float b, float tolerance = 0.002f) { return std::abs(a - b) < tolerance; }

static auto plane(float slope)
{
    return [slope](float x, float, Surface& out)
    {
        out.height = 500.0f + x * slope;
        out.normal = Vector3(-slope, 1, 0).Normalized();
        out.weight = 1;
        return true;
    };
}

static void sort_transforms(std::vector<Matrix>& transforms)
{
    std::sort(transforms.begin(), transforms.end(), [](const Matrix& a, const Matrix& b)
    {
        const Vector3 p = a.GetTranslation(), q = b.GetTranslation();
        return p.x == q.x ? p.z < q.z : p.x < q.x;
    });
}

int main()
{
    static_assert(sizeof(Instance) == sizeof(PackedInstance));
    static_assert(offsetof(Instance, rotation_xy) == offsetof(PackedInstance, rotation_xy));
    static_assert(offsetof(Instance, scale_z) == offsetof(PackedInstance, scale_z));
    assert(Instance::GetIdentity().GetMatrix() == Matrix::Identity);
    // The actual renderer representation must preserve a wide cliff, its burial and its normal.
    // Previously an average scale (clamped to 100) silently replaced all three authored axes.
    for (const Vector3& scale : {Vector3(280, 45, 85), Vector3(0.002f, 0.03f, 0.01f), Vector3(1,1,1)})
    for (const Vector3& angles : {Vector3(23,177,12), Vector3(0,0,0), Vector3(180,90,0)})
    {
        const Matrix authored = Matrix::CreateScale(scale) * Matrix::CreateRotation(Quaternion::FromEulerAngles(angles)) *
            Matrix::CreateTranslation(Vector3(12345, 321, -5432));
        Instance packed;
        packed.SetMatrix(authored);
        const Matrix decoded = packed.GetMatrix();
        assert((decoded.GetScale() - scale).Length() < 0.001f);
        for (const Vector3& p : {Vector3::Zero, Vector3(1,0,0), Vector3(0,1,0), Vector3(0,0,1)})
            assert((authored * p - decoded * p).Length() < 0.02f);
    }
    // A twisted 25 m cell: bilinear gives 6.25 m here, but the rendered diagonal is at zero.
    assert(triangle_height(0, 0, 0, 100, 0.25f, 0.25f) == 0);
    assert(triangle_height(0, 0, 0, 100, 0.75f, 0.75f) == 50);
    std::vector<Vector3> grid = {{0,0,0}, {25,0,0}, {0,0,25}, {25,100,25}};
    TerrainGridMapping mapping;
    mapping.scale_x = mapping.scale_z = 25;
    assert(TerrainSystem::SampleHeight(grid, 2, 2, 6.25f, 6.25f, mapping) == 0);
    assert(TerrainSystem::SampleHeight(grid, 2, 2, 18.75f, 18.75f, mapping) == 50);
    // The same plane normal at both boundaries and the middle; edge clamping must not halve it.
    grid = {{0,0,0}, {25,25,0}, {0,0,25}, {25,25,25}};
    for (float x : {0.0f, 12.5f, 25.0f})
    {
        const Vector3 normal = TerrainSystem::SampleNormal(grid, 2, 2, x, 12.5f, mapping);
        assert(close(normal.x, -std::sqrt(0.5f)) && close(normal.y, std::sqrt(0.5f)));
    }

    TerrainScatterLayer layer;
    layer.density = 6;
    layer.clump_count = 12;
    layer.formation_spacing = 100;
    layer.formation_length = 120;
    layer.formation_width = 60;
    layer.formation_height = 45;
    layer.mesh_scale = 8;
    layer.size_min = 0.5f;
    layer.size_max = 1.0f;
    layer.align_to_normal = 0.65f;
    layer.slope_min = 20;
    layer.slope_max = 80;
    layer.max_per_tile = 0;
    const BoundingBox bounds(Vector3(-1,-1,-1), Vector3(1,1,1));
    std::vector<Matrix> whole, left, right, again;
    auto hillside = plane(0.6f);
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, whole);
    assert(whole.size() > 200);
    float smallest = 1e9f, largest = 0;
    size_t slabs = 0, fragments = 0, joined_slabs = 0;
    for (const Matrix& m : whole)
    {
        const float span = m.GetScale().x * 2.0f;
        smallest = std::min(smallest, span);
        largest = std::max(largest, span);
        fragments += span < 20;
        if (span < 55) continue;
        ++slabs;
        // Bedrock must have neighbours close enough to overlap, not merely be larger nuggets.
        for (const Matrix& n : whole)
        {
            if (&m == &n || n.GetScale().x * 2.0f < 55) continue;
            if ((m.GetTranslation() - n.GetTranslation()).Length() < (m.GetScale().x + n.GetScale().x) * 0.7f)
            {
                ++joined_slabs;
                break;
            }
        }
    }
    assert(largest > 75 && largest / smallest > 12);
    assert(slabs > 20 && fragments > 100 && joined_slabs * 4 > slabs * 3);
    mountain_formations(layer, bounds, -400, -400, 0, 400, 0, Vector3(-200,0,0), hillside, left);
    mountain_formations(layer, bounds, 0, -400, 400, 400, 0, Vector3(200,0,0), hillside, right);
    for (Matrix& m : left) m = m * Matrix::CreateTranslation(Vector3(-200,0,0));
    for (Matrix& m : right) m = m * Matrix::CreateTranslation(Vector3(200,0,0));
    left.insert(left.end(), right.begin(), right.end());
    sort_transforms(whole); sort_transforms(left);
    assert(whole.size() == left.size());
    for (size_t i = 0; i < whole.size(); ++i)
    {
        assert((whole[i].GetTranslation() - left[i].GetTranslation()).Length() < 0.002f);
        const Vector3 c = whole[i].GetTranslation();
        const Vector3 half = whole[i].GetScale();
        assert(std::isfinite(c.x + c.y + c.z));
        assert(half.x > 0 && half.y > 0 && half.z > 0);
    }
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, again);
    sort_transforms(again);
    assert(whole == again);
    ++layer.seed;
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, again);
    sort_transforms(again);
    assert(whole != again);

    // Changing asset units must not change the authored cliff dimensions or placement.
    const BoundingBox centimetres(Vector3(-100,-100,-100), Vector3(100,100,100));
    TerrainScatterLayer cores_only = layer;
    cores_only.clump_count = 3;
    std::vector<Matrix> metres, centimetre_mesh;
    mountain_formations(cores_only, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, metres);
    mountain_formations(cores_only, centimetres, -400, -400, 400, 400, 0, Vector3::Zero, hillside, centimetre_mesh);
    assert(!metres.empty() && metres.size() == centimetre_mesh.size());
    for (size_t i = 0; i < metres.size(); ++i)
    {
        assert((metres[i].GetTranslation() - centimetre_mesh[i].GetTranslation()).Length() < 0.002f);
        assert((metres[i].GetScale() - centimetre_mesh[i].GetScale() * 100.0f).Length() < 0.002f);
    }

    // A narrow strip of preferred anchors may stand on a wider, less steep toe. Hard exclusions
    // at the same location must still reject these large footprints.
    auto toe = [&](float x, float z, Surface& out)
    {
        hillside(x, z, out);
        out.weight = std::abs(x - 50.0f) < 12.0f ? 1.0f : 0.0f;
        out.allowed = true;
        return true;
    };
    mountain_formations(cores_only, bounds, -400, -400, 400, 400, 0, Vector3::Zero, toe, again);
    assert(!again.empty());
    auto blocked_toe = [&](float x, float z, Surface& out)
    {
        toe(x, z, out);
        out.allowed = out.weight > 0.0f;
        return true;
    };
    mountain_formations(cores_only, bounds, -400, -400, 400, 400, 0, Vector3::Zero, blocked_toe, again);
    assert(again.empty());

    auto excluded = [](float, float, Surface& out) { out.weight = 0; return true; };
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, excluded, again);
    assert(again.empty());
    auto outside = [](float, float, Surface&) { return false; };
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, outside, again);
    assert(again.empty());
    layer.max_per_tile = 17;
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, again);
    assert(again.size() == 17);
    layer.density = 0;
    mountain_formations(layer, bounds, -400, -400, 400, 400, 0, Vector3::Zero, hillside, again);
    assert(again.empty());

    std::cout << "Terrain regressions passed: height/normal sampling, tile seams, deterministic formations, scale hierarchy, connected slabs, asset units, footprint exclusions, budgets\n";
}
