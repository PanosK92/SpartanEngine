// Shared by population, indirect-argument generation and GPU allocation tests.
#ifndef GRASS_LOD_ALLOCATION_H
#define GRASS_LOD_ALLOCATION_H
static const uint grass_ring_count = 9u; // renderer_max_gpu_scatter_args, asserted on the CPU

bool grass_reserve_instance(uint ring, uint capacity)
{
    // A population dispatch has one ring. Only surviving lanes reach this
    // function, so reserve their contiguous range with one atomic per wave.
    uint base = 0u;
    uint count = WaveActiveCountBits(true);
    uint offset = WavePrefixCountBits(true);
    if (WaveIsFirstLane())
        InterlockedAdd(grass_count[ring], count, base);
    return WaveReadLaneFirst(base) + offset < capacity;
}

uint grass_allocate_detail_bin(uint ring, uint capacity, bool coarse)
{
    uint fine_base = 0u;
    uint coarse_base = 0u;
    uint fine_count = WaveActiveCountBits(!coarse);
    uint coarse_count = WaveActiveCountBits(coarse);
    uint fine_offset = WavePrefixCountBits(!coarse);
    uint coarse_offset = WavePrefixCountBits(coarse);
    if (WaveIsFirstLane())
    {
        if (fine_count != 0u)
            InterlockedAdd(grass_count[ring + grass_ring_count], fine_count, fine_base);
        if (coarse_count != 0u)
            InterlockedAdd(grass_count[ring + grass_ring_count * 2u], coarse_count, coarse_base);
    }
    fine_base = WaveReadLaneFirst(fine_base);
    coarse_base = WaveReadLaneFirst(coarse_base);
    uint bin_slot = coarse ? coarse_base + coarse_offset : fine_base + fine_offset;
    return coarse ? capacity - 1u - bin_slot : bin_slot;
}

uint2 grass_detail_counts(uint ring, uint capacity)
{
    uint fine = min(grass_count[ring + grass_ring_count], capacity);
    uint coarse = min(grass_count[ring + grass_ring_count * 2u], capacity - fine);
    return uint2(fine, coarse);
}
#endif
