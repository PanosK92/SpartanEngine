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
        constexpr const char* file_path_name = "data/profiling/ProfilingReport.csv";

        std::ofstream csv_export_file;
        filesystem::path file_path(file_path_name);
        int current_mode_hardware   = 0;
        int current_csv_col         = 0; // frames by columns
        int current_csv_row         = 0; // frame data by row
    }

    void CsvExporter::StartRecording(int mode_hardware)
    {
        if (csv_export_file.is_open())
        {
            SP_LOG_WARNING("File is already open and recording data.");
            return;
        }

        try
        {
            if (file_path.has_parent_path())
            {
                filesystem::create_directories(file_path.parent_path());
            }

            csv_export_file.exceptions(std::ofstream::failbit | std::ofstream::badbit);

            csv_export_file.open(file_path, ios::out | ios::trunc);

            if (current_csv_col == 0)
            {
                csv_export_file << "Frame ID,";
                csv_export_file.flush();
                current_csv_col++;
            }
            current_mode_hardware = mode_hardware;

            SP_LOG_INFO("Started recording %s data for CSV report: %s", current_mode_hardware == 0 ? "GPU" : "CPU", file_path.generic_string().c_str());
        }
        catch (const filesystem::filesystem_error& e) // directory creation failures
        {
            SP_LOG_ERROR("Filesystem Error: %s", e.what());
        }
        catch (const ios_base::failure& e) // file opening or writing failures
        {
            SP_LOG_ERROR("File I/O Error: %s", e.what());
        }
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

            // Write on file directly. Can still store data even during a crash.
            csv_export_file.flush();
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

    void CsvExporter::StopRecording(bool has_data_changed)
    {
        if (csv_export_file.is_open())
        {
            csv_export_file.flush();
            csv_export_file.close();
            current_csv_col = 0;
            current_csv_row = 0;
            if (has_data_changed)
            {
                SP_LOG_WARNING("Stopped recording data for CSV report %s. Time block data were modified.", file_path.generic_string().c_str());
            }
            else
            {
                SP_LOG_INFO("Stopped recording %s data for CSV report: %s", current_mode_hardware == 0 ? "GPU" : "CPU", file_path.generic_string().c_str());
            }
        }
        else if (!csv_export_file.is_open() && !has_data_changed)
        {
            SP_LOG_WARNING("Invalid action. There is no active CSV recording.");
        }
    }
}
