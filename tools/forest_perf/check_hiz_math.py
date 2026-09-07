"""Independent coverage-oracle checks for the conservative Hi-Z algorithm.

These are CPU reference properties, not a GPU execution test. Shader compilation
and the live occlusion-on/off captures are separate checks.
"""
import math
import random

rng = random.Random(37041)

def pyramid(width, height):
    # Track exactly which original pixels feed each reduced cell, not depth.
    levels = [[[1 << (y * width + x) for x in range(width)] for y in range(height)]]
    while width > 1 or height > 1:
        dw, dh = max(width // 2, 1), max(height // 2, 1)
        dst = [[0] * dw for _ in range(dh)]
        for y in range(dh):
            for x in range(dw):
                for sy in range(y * height // dh, ((y + 1) * height + dh - 1) // dh):
                    for sx in range(x * width // dw, ((x + 1) * width + dw - 1) // dw):
                        dst[y][x] |= levels[-1][sy][sx]
        levels.append(dst)
        width, height = dw, dh
    return levels

def query_rect(width, height, uv0, uv1, scale, max_level):
    lo = [max(0, uv0[i] * scale[i] - 1 / size) for i, size in enumerate((width, height))]
    hi = [min(1, uv1[i] * scale[i] + 1 / size) for i, size in enumerate((width, height))]
    extent = max((hi[0] - lo[0]) * width, (hi[1] - lo[1]) * height, 1)
    mip = min(max_level, math.ceil(math.log2(extent)))
    dims = [max(width >> mip, 1), max(height >> mip, 1)]
    cell_lo = [min(int(lo[i] * dims[i]), dims[i] - 1) for i in range(2)]
    cell_hi = [min(int(hi[i] * dims[i]), dims[i] - 1) for i in range(2)]
    assert all(cell_hi[i] - cell_lo[i] <= 1 for i in range(2)), 'Four taps must cover all touched cells'
    return mip, cell_lo, cell_hi

queries = 0
for width, height in [(1, 1), (1, 17), (17, 1), (3, 5), (9, 7), (32, 32), (47, 31)]:
    levels = pyramid(width, height)
    assert levels[-1][0][0] == (1 << (width * height)) - 1, 'No odd-edge pixel may be lost'
    for _ in range(400):
        u = sorted((rng.random(), rng.random()))
        v = sorted((rng.random(), rng.random()))
        scale = rng.choice([(1, 1), (0.5, 0.5), (0.73, 0.68)])
        mip, lo, hi = query_rect(width, height, (u[0], v[0]), (u[1], v[1]), scale, len(levels) - 1)
        covered = 0
        for y in {lo[1], hi[1]}:
            for x in {lo[0], hi[0]}:
                covered |= levels[mip][y][x]
        # Brute-force oracle: every source pixel overlapping the unexpanded box
        # must be represented, including a hypothetical background hole there.
        for y in range(height):
            for x in range(width):
                overlaps = ((x + 1) / width > u[0] * scale[0] and x / width < u[1] * scale[0]
                            and (y + 1) / height > v[0] * scale[1] and y / height < v[1] * scale[1])
                if overlaps:
                    assert covered & (1 << (y * width + x)), 'An interior hole was omitted'
        queries += 1

for width, height in [(1920, 938), (3180, 1555), (1, 4097), (4097, 1), (3840, 2160)]:
    for _ in range(2000):
        u, v = sorted((rng.random(), rng.random())), sorted((rng.random(), rng.random()))
        query_rect(width, height, (u[0], v[0]), (u[1], v[1]), (0.73, 0.81), int(math.log2(max(width, height))))
        queries += 1

def visible(nearest, occluder, floor=2e-7):
    return nearest >= occluder - max(floor, abs(nearest) * 1e-4)

for distance in [20, 100, 1000, 10000]:
    z = 0.1 / distance
    assert visible(z, 0), 'Sky cannot occlude'
    assert visible(z, z), 'Equal-depth/self surfaces must survive'
    assert visible(z, z * 0.5), 'Farther geometry cannot occlude nearer geometry'
    assert not visible(z, z * 2), 'A fully covering nearer wall must occlude'
    assert visible(z, z * 2, 0.01), 'Regression: the old bias fails to occlude distant geometry'
print(f'PASS {queries} coverage queries, odd/thin dimensions, dynamic scale, interior holes and reverse-Z ordering')
