// Progressive stratification: distinct jittered subcells, with an independently
// scrambled quadtree in every world cell. Existing roots never move when the
// camera's FOV changes the number of candidates we draw from this sequence.
uint grass_distribution_hash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

float2 grass_stratified_position(uint index, uint capacity, float2 random, uint seed)
{
    uint levels = (uint)ceil(log2((float)max(capacity, 1u)) * 0.5);
    uint2 cell = 0u;
    uint path = seed;
    [loop] for (uint level = 0; level < levels; ++level)
    {
        path = grass_distribution_hash(path);
        uint quadrant = ((index >> (2u * level)) & 3u) ^ (path & 3u);
        cell = cell * 2u + uint2(quadrant & 1u, quadrant >> 1u);
        path ^= quadrant + 0x9e3779b9u;
    }
    float2 jitter = lerp(0.12, 0.88, random);
    return (float2(cell) + jitter) / (float)(1u << levels);
}
