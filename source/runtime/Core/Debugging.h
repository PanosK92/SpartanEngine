/*
Copyright(c) 2015-2025 Panos Karabelas

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
        inline static bool m_validation_layer_enabled        = false; // enables vulkan diagnostic layers, incurs significant per-draw cpu performance overhead
        inline static bool m_gpu_assisted_validation_enabled = false; // performs gpu-based validation with substantial cpu and gpu performance impact
        inline static bool m_logging_to_file_enabled         = false; // writes diagnostic logs to disk, causes high cpu overhead due to file I/O operations
        inline static bool m_breadcrumbs_enabled             = false; // tracks gpu crash information in breadcrumbs.txt, minimal overhead (amd gpus only) - crashes in debug mode - outputs unreliable data in release mode - issue reported to amd
        inline static bool m_renderdoc_enabled               = true; // integrates RenderDoc graphics debugging, introduces high cpu overhead from api wrapping
        inline static bool m_gpu_marking_enabled             = true;  // enables gpu resource marking with negligible performance cost
        inline static bool m_gpu_timing_enabled              = true;  // enables gpu performance timing with negligible performance cost
        inline static bool m_shader_optimization_enabled     = true;  // controls shader optimization, disabling has significant performance impact
    };
}
