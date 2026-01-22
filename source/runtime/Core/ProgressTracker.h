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

#pragma once

//= INCLUDES ====
#include <atomic>
//===============

namespace spartan
{
    enum class ProgressType
    {
        ModelImporter,
        World,
        Terrain,
        Download,
        Max
    };

    class Progress
    {
    public:
        void Start(const uint32_t job_count, const std::string& text);

        float GetFraction() const;
        void SetFraction(float fraction); // for continuous progress (0.0 to 1.0)
        void JobDone();

        const std::string& GetText();
        void SetText(const std::string& text);

        bool IsProgressing() const;

    private:
        std::atomic<uint32_t> m_jobs_done = 0;
        std::atomic<uint32_t> m_job_count = 0;
        std::atomic<float> m_fraction     = 1.0f; // for continuous mode
        bool m_continuous_mode            = false;
        std::string m_text;
    };

    class  ProgressTracker
    {
    public:
        static Progress& GetProgress(const ProgressType progress_type);
        static bool IsLoading();
        static void SetGlobalLoadingState(const bool is_loading);
    };
}
