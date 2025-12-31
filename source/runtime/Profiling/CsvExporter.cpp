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

//= INCLUDES ========================
#include "pch.h"
#include "CsvExporter.h"

#include "TimeBlock.h"
//===================================

//= NAMESPACES ===============
using namespace std;
//============================

namespace spartan
{
    namespace
    {
        string file_path = "data/profiling/ProfilingReport.csv";
        ofstream csv_export_file(file_path);
        int current_csv_col = 0; // frames by columns
        int current_csv_row = 0; // frame data by row
    }

    void CsvExporter::StartRecording()
    {
        if (csv_export_file.is_open())
        {
            SP_LOG_INFO("File is already open and recording data.");
            return;
        }

        csv_export_file.open("ProfilingReport.csv", ios::out | ios::trunc);
        if (!csv_export_file.good() || !csv_export_file.is_open())
        {
            SP_LOG_ERROR("Failed to open CSV file: %s", file_path.c_str());
            return;
        }

        if (current_csv_col == 0)
        {
            csv_export_file << "Frame ID,";
            current_csv_col++;
        }

        SP_LOG_INFO("Started recording data for CSV report.");
    }

    void CsvExporter::WriteFrameData(const TimeBlock& current_time_block, const uint64_t frame_number)
    {
        if (csv_export_file.is_open())
        {
            if (current_csv_row == 0)
            {
                csv_export_file << current_time_block.GetName() << ",";
                return;
            }
            if (current_csv_col == 0)
            {
                csv_export_file  << frame_number << ",";
            }
            csv_export_file << current_time_block.GetDuration() << ",";
            current_csv_col++;
        }
    }

    void CsvExporter::NextFrame()
    {
        if (csv_export_file.is_open())
        {
            csv_export_file << "\n";
            current_csv_row++;
            current_csv_col = 0;
        }
    }

    void CsvExporter::StopRecording(bool is_reset)
    {
        if (csv_export_file.is_open())
        {
            if (is_reset)
            {
                current_csv_col = 0;
                current_csv_row = 0;
            }
            csv_export_file.flush();
            csv_export_file.close();
            SP_LOG_INFO("Stopped recording data for CSV report.");
        }
        else
        {
            SP_LOG_WARNING("Invalid action. There is no active CSV recording.");
        }
    }
}
