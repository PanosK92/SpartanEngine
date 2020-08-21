/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===========================
#include <string>
#include <unordered_map>
#include "../Core/Spartan_Definitions.h"
//======================================

namespace Spartan
{
    static int g_progress_model_importer    = 0;
    static int g_progress_world                = 1;
    static int g_progress_resource_cache    = 2;

    struct Progress
    {
        Progress(){ Clear(); }

        void Clear()
        {
            status.clear();
            jobsDone    = 0;
            jobCount    = 0;
            isLoading    = false;
        }

        std::string status;
        int jobsDone;
        int jobCount;
        bool isLoading;
    };

    class SPARTAN_CLASS ProgressReport
    {
    public:
        static ProgressReport& Get()
        {
            static ProgressReport instance;
            return instance;
        }

        ProgressReport() = default;

        void Reset(int progressID)
        {
            m_reports[progressID].Clear();
        }

        const std::string& GetStatus(int progressID)                { return m_reports[progressID].status; }
        void SetStatus(int progressID, const std::string& status)    { m_reports[progressID].status = status; }
        void SetJobCount(int progressID, int jobCount)                { m_reports[progressID].jobCount = jobCount;}
        void IncrementJobsDone(int progressID)                        { m_reports[progressID].jobsDone++; }
        void SetJobsDone(int progressID, int jobsDone)                { m_reports[progressID].jobsDone = jobsDone; }
        float GetPercentage(int progressID)                            { return static_cast<float>(m_reports[progressID].jobsDone) / static_cast<float>(m_reports[progressID].jobCount); }
        bool GetIsLoading(int progressID)                            { return m_reports[progressID].isLoading; }
        void SetIsLoading(int progressID, bool isLoading)            { m_reports[progressID].isLoading = isLoading; }

    private:    
        std::unordered_map<int, Progress> m_reports;
    };
}
