/*
Copyright(c) 2016-2025 Panos Karabelas

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

#pragma once

namespace spartan
{
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

    private:
        inline static bool m_validation_layer_enabled        = true; // enables Vulkan diagnostic layers, incurs significant per-draw CPU performance overhead
        inline static bool m_gpu_assisted_validation_enabled = true; // performs GPU-based validation with substantial CPU and GPU performance impact
        inline static bool m_logging_to_file_enabled         = true; // writes diagnostic logs to disk, causes high CPU overhead due to file I/O operations
        inline static bool m_breadcrumbs_enabled             = false; // tracks GPU crash information in breadcrumbs.txt, minimal overhead (AMD GPUs only)
        inline static bool m_renderdoc_enabled               = false; // integrates RenderDoc graphics debugging, introduces high CPU overhead from API wrapping
        inline static bool m_gpu_marking_enabled             = true;  // enables GPU resource marking with negligible performance cost
        inline static bool m_gpu_timing_enabled              = true;  // enables GPU performance timing with negligible performance cost
        inline static bool m_shader_optimization_enabled     = true;  // controls shader optimization, disabling has significant performance impact
    };
}
