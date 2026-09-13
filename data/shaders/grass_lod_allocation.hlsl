// Shared by population, indirect-argument generation and GPU allocation tests.
#ifndef GRASS_LOD_ALLOCATION_H
#define GRASS_LOD_ALLOCATION_H
static const uint grass_ring_count = 9u; // renderer_max_gpu_scatter_args, asserted on the CPU

bool grass_reserve_instance(uint ring, uint capacity)
{
    uint total_slot;
    InterlockedAdd(grass_count[ring], 1u, total_slot);
    return total_slot < capacity;
}

uint grass_allocate_detail_bin(uint ring, uint capacity, bool coarse)
{
    uint bin_slot;
    InterlockedAdd(grass_count[ring + grass_ring_count * (coarse ? 2u : 1u)], 1u, bin_slot);
    return coarse ? capacity - 1u - bin_slot : bin_slot;
}

uint2 grass_detail_counts(uint ring, uint capacity)
{
    uint fine = min(grass_count[ring + grass_ring_count], capacity);
    uint coarse = min(grass_count[ring + grass_ring_count * 2u], capacity - fine);
    return uint2(fine, coarse);
}
#endif
