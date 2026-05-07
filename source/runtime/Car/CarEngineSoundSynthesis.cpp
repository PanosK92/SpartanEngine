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

//= INCLUDES =====================================
#include "pch.h"
#include "CarEngineSoundSynthesis.h"
#include "CarTireSquealSynthesis.h"
#include "../../editor/ImGui/Source/imgui.h"
//================================================

namespace engine_sound
{
    void debug_window()
    {
        if (!ImGui::Begin("Sound Synthesis", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }

        auto draw_level_bar = [](const char* label, float level, ImU32 color)
        {
            ImGui::Text("  %s:", label);
            ImGui::SameLine(120);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_width = 200.0f;
            float bar_height = 14.0f;
            float fill = std::clamp(level * 5.0f, 0.0f, 1.0f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(pos, ImVec2(pos.x + bar_width, pos.y + bar_height), IM_COL32(40, 40, 40, 255));
            draw_list->AddRectFilled(pos, ImVec2(pos.x + bar_width * fill, pos.y + bar_height), color);
            draw_list->AddRect(pos, ImVec2(pos.x + bar_width, pos.y + bar_height), IM_COL32(80, 80, 80, 255));

            ImGui::Dummy(ImVec2(bar_width, bar_height));
            ImGui::SameLine();
            ImGui::Text("%.4f", level);
        };

        // draws one trace into a shared rect, auto-normalised by per-trace peak
        auto draw_trace = [](ImVec2 pos, float width, float height, const float* buf, int count, int start, ImU32 color)
        {
            float peak = 1e-6f;
            for (int i = 0; i < count; ++i) peak = std::max(peak, fabsf(buf[i]));
            float scale = 0.45f / peak;

            float center_y = pos.y + height * 0.5f;
            float x_step   = width / (float)count;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            for (int i = 0; i < count - 1; ++i)
            {
                int idx0 = (start + i) % count;
                int idx1 = (start + i + 1) % count;
                float x0 = pos.x + i * x_step;
                float x1 = pos.x + (i + 1) * x_step;
                float y0 = center_y - buf[idx0] * height * scale;
                float y1 = center_y - buf[idx1] * height * scale;
                draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
            }
        };

        const debug_data& dbg = get_debug();
        synthesizer& s        = get_synthesizer();

        // ---- engine info ----
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Engine");
        ImGui::Text("  RPM: %.0f  |  Throttle: %.0f%%  |  Boost: %.2f bar  |  Firing: %.1f Hz",
            dbg.rpm, dbg.throttle * 100.0f, dbg.boost, dbg.firing_freq);

        if (dbg.ir_taps > 0)
            ImGui::Text("  Exhaust IR: %d taps loaded @ %.0f Hz", dbg.ir_taps, (float)tuning::sample_rate);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "  Exhaust IR: NOT LOADED (check binaries/project/music/exhaust_ir.wav)");

        ImGui::Separator();

        // ---- live tuning sliders ----
        if (ImGui::CollapsingHeader("Live Tuning", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("df_f_mix",         &s.params.df_f_mix,         0.0f, 1.0f, "%.4f");
            ImGui::SliderFloat("air_noise",        &s.params.air_noise,        0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("convolution_wet",  &s.params.convolution_wet,  0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("combustion_level", &s.params.combustion_level, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("exhaust_level",    &s.params.exhaust_level,    0.0f, 4.0f, "%.3f");
            ImGui::SliderFloat("drive_extra",      &s.params.drive_extra,     -2.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("leveler_target",   &s.params.leveler_target,   0.05f, 1.5f, "%.3f");
            ImGui::SliderFloat("master_offset",    &s.params.master_offset,   -0.7f, 1.0f, "%.2f");
            ImGui::SliderFloat("notch_depth",      &s.params.notch_depth,      0.0f, 1.0f, "%.2f");
            if (ImGui::Button("Reset to defaults"))
                s.params = runtime_params();
        }

        ImGui::Separator();

        // ---- per-layer levels ----
        draw_level_bar("Combustion", dbg.combustion_level, IM_COL32(255, 100, 100, 255));
        draw_level_bar("Exhaust",    dbg.exhaust_level,    IM_COL32(255, 180, 100, 255));
        draw_level_bar("Induction",  dbg.induction_level,  IM_COL32(100, 200, 255, 255));
        draw_level_bar("Mechanical", dbg.mechanical_level, IM_COL32(200, 200, 100, 255));
        draw_level_bar("Turbo",      dbg.turbo_level,      IM_COL32(100, 255, 200, 255));

        ImGui::Separator();

        // ---- leveler meter ----
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 1.0f, 1.0f), "Auto-Leveler");
        draw_level_bar("Gain",     dbg.leveler_gain * 0.2f,     IM_COL32(220, 130, 255, 255));
        ImGui::SameLine(); ImGui::Text(" (raw=%.3f)", dbg.leveler_gain);
        draw_level_bar("Envelope", dbg.leveler_envelope,        IM_COL32(180, 100, 220, 255));

        ImGui::Separator();

        // ---- wav dump ----
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "WAV Dump (auto-saves to binaries/last_synth.wav)");
        static double last_save_time = -1.0;
        static bool   last_save_ok   = false;
        double now = ImGui::GetTime();

        // auto-save once capture is ready
        if (dbg.dump_ready)
        {
            last_save_ok   = s.save_dump("last_synth.wav");
            last_save_time = now;
        }

        if (dbg.dump_total == 0)
        {
            if (ImGui::Button("Dump 2s WAV"))  s.begin_dump(2.0f);
            ImGui::SameLine();
            if (ImGui::Button("Dump 5s WAV"))  s.begin_dump(5.0f);
            ImGui::SameLine();
            if (ImGui::Button("Dump 10s WAV")) s.begin_dump(10.0f);
        }
        else
        {
            float pct = (float)dbg.dump_progress / (float)dbg.dump_total;
            ImGui::ProgressBar(pct, ImVec2(300, 0), "capturing...");
        }

        if (last_save_time > 0 && now - last_save_time < 6.0)
        {
            if (last_save_ok)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                    "saved binaries/last_synth.wav (%.1fs ago)", now - last_save_time);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "save FAILED (check working dir / permissions)");
        }

        ImGui::Separator();

        // ---- multi-trace waveform ----
        ImGui::Text("Engine Waveform (green=output, red=cyl bank L, orange=conv input, cyan=conv output)");
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float width = 480.0f;
            float height = 100.0f;
            float center_y = pos.y + height * 0.5f;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 25, 255));
            draw_list->AddLine(ImVec2(pos.x, center_y), ImVec2(pos.x + width, center_y), IM_COL32(60, 60, 60, 255));

            int start = dbg.waveform_write_pos;
            int n     = debug_data::waveform_size;
            draw_trace(pos, width, height, dbg.waveform_cyl, n, start, IM_COL32(255, 100, 100, 200));
            draw_trace(pos, width, height, dbg.waveform_vin, n, start, IM_COL32(255, 180, 100, 200));
            draw_trace(pos, width, height, dbg.waveform_exh, n, start, IM_COL32(100, 220, 255, 200));
            draw_trace(pos, width, height, dbg.waveform,     n, start, IM_COL32(100, 255, 100, 255));

            draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(80, 80, 80, 255));
            ImGui::Dummy(ImVec2(width, height));
        }

        // ---- spectrum ----
        ImGui::Text("Spectrum (0..%.0f Hz, log mag, -80..0 dB)", (float)tuning::sample_rate * 0.5f);
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float width = 480.0f;
            float height = 100.0f;
            int   bins = debug_data::spectrum_bins;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 25, 255));

            // grid lines at -20, -40, -60 db
            for (int g = 1; g < 4; ++g)
            {
                float gy = pos.y + height * ((float)g / 4.0f);
                draw_list->AddLine(ImVec2(pos.x, gy), ImVec2(pos.x + width, gy), IM_COL32(50, 50, 50, 255));
            }

            // grid lines at common frequencies (50,100,500,1k,2k,5k,10k Hz)
            const float freqs[] = { 50.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
            float nyquist = (float)tuning::sample_rate * 0.5f;
            for (float f : freqs)
            {
                float fx = pos.x + width * (f / nyquist);
                draw_list->AddLine(ImVec2(fx, pos.y), ImVec2(fx, pos.y + height), IM_COL32(50, 50, 60, 200));
            }

            float x_step = width / (float)bins;
            for (int i = 0; i < bins - 1; ++i)
            {
                float db0 = std::clamp(dbg.spectrum[i],     -80.0f, 0.0f);
                float db1 = std::clamp(dbg.spectrum[i + 1], -80.0f, 0.0f);
                float y0 = pos.y + height * (1.0f - (db0 + 80.0f) / 80.0f);
                float y1 = pos.y + height * (1.0f - (db1 + 80.0f) / 80.0f);
                float x0 = pos.x + i * x_step;
                float x1 = pos.x + (i + 1) * x_step;
                draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(180, 220, 255, 220), 1.0f);
            }

            // mark firing fundamental
            float fx = pos.x + width * (dbg.firing_freq / nyquist);
            if (fx >= pos.x && fx <= pos.x + width)
                draw_list->AddLine(ImVec2(fx, pos.y), ImVec2(fx, pos.y + height), IM_COL32(255, 200, 80, 220), 1.0f);

            draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(80, 80, 80, 255));
            ImGui::Dummy(ImVec2(width, height));
        }

        ImGui::Separator();

        // ---- tire squeal section ----
        const tire_squeal_sound::debug_data& tire_dbg = tire_squeal_sound::get_debug();

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tire Squeal");
        ImGui::Text("  Intensity: %.0f%%  |  Speed: %.0f%%",
            tire_dbg.intensity * 100.0f, tire_dbg.speed_norm * 100.0f);

        draw_level_bar("Screech",    tire_dbg.screech_level, IM_COL32(255, 100, 180, 255));
        draw_level_bar("Sibilance",  tire_dbg.sibilance_lvl, IM_COL32(200, 130, 255, 255));
        draw_level_bar("Body",       tire_dbg.body_level,    IM_COL32(180, 180, 100, 255));

        ImGui::Separator();

        // ---- combined output ----
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Output");
        draw_level_bar("Engine",   dbg.output_level,        IM_COL32(100, 255, 100, 255));
        draw_level_bar("Tire",     tire_dbg.output_level,   IM_COL32(180, 100, 255, 255));
        draw_level_bar("Eng Peak", dbg.output_peak,         IM_COL32(255, 255, 100, 255));
        draw_level_bar("Tire Peak", tire_dbg.output_peak,   IM_COL32(255, 200, 255, 255));

        ImGui::End();
    }
}
