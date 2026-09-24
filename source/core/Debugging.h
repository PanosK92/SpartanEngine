/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    // build time toggles for the validation, instrumentation and tooling paths
    class Debugging
    {
    public:
        static bool IsValidationLayerEnabled()       { return m_validation_layer_enabled; }
        static bool IsGpuAssistedValidationEnabled() { return m_gpu_assisted_validation_enabled; }
        static bool IsGpuMarkingEnabled()            { return m_gpu_marking_enabled; }
        static bool IsGpuTimingEnabled()             { return m_gpu_timing_enabled; }
        static bool IsRenderdocEnabled()             { return m_renderdoc_enabled; }
        static bool IsShaderOptimizationEnabled()    { return m_shader_optimization_enabled; }
        static bool IsLoggingToFileEnabled()         { return m_logging_to_file_enabled; }
        static bool IsBreadcrumbsEnabled()           { return m_breadcrumbs_enabled; }
        static bool IsSteamEnabled()                 { return m_steam_enabled; }
        static bool IsD3D12EnhancedBarriersEnabled() { return m_d3d12_enhanced_barriers_enabled; }

    private:
        inline static bool m_validation_layer_enabled        = false; // enables debug/validation layer for api error detection and debug message reporting
        inline static bool m_gpu_assisted_validation_enabled = false; // gpu-based validation is extremely slow and breaks on first error, use the debug/validation layer alone for day to day work
        inline static bool m_logging_to_file_enabled         = false; // writes diagnostic and validation messages to a persistent log file
        inline static bool m_breadcrumbs_enabled             = true; // records gpu execution markers to help identify the cause of gpu crashes
        inline static bool m_renderdoc_enabled               = false; // enables integration with renderdoc for frame capture and gpu debugging
        inline static bool m_gpu_marking_enabled             = false; // enable only while capturing with an external gpu debugger
        inline static bool m_gpu_timing_enabled              = true;  // measures gpu execution times for profiling and performance analysis
        inline static bool m_shader_optimization_enabled     = true;  // enables shader compiler optimizations to improve performance and efficiency
        inline static bool m_steam_enabled                   = true;  // initializes steamworks, tick only runs cheap callback pumping when active
        inline static bool m_d3d12_enhanced_barriers_enabled = false; // d3d12 only, submits barriers through ID3D12GraphicsCommandList7 instead of legacy ResourceBarrier, see D3D12_Barriers.cpp for the two interop prerequisites before enabling
    };
}
