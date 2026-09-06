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

// Render the production telemetry drawing code without a world, GPU or running editor.
#include "car/CarTelemetry.h"
#define FREEIMAGE_LIB
#include "FreeImage/FreeImage.h"
#include <cassert>
#include <vector>

using namespace spartan::car_hud::telemetry;

static void check_history()
{
    snapshot s;
    s.vehicle_id = 1;
    history h;
    for (int i = 0; i < 1800; ++i)
        h.update(s, i / 120.0);
    assert(h.count <= history::capacity && h.count > 150);
    for (int i = 1; i < h.count; ++i)
        assert(h.at(i).time > h.at(i - 1).time);
    assert(h.at(h.count - 1).time - h.at(0).time <= 10.1);
    h.update(s, 20); // closing/reopening must not draw through missing data
    assert(h.count == 1);
    s.vehicle_id = 2;
    h.update(s, 20.1);
    assert(h.count == 1);
    s.distance = 10;
    h.update(s, 20.2);
    s.distance = 0;
    h.update(s, 20.3);
    assert(h.count == 1);
    h.update(s, 1); // clock reset
    assert(h.count == 1);
    s.full_simulation = false;
    h.update(s, 1.1);
    assert(h.count == 1);
    s.name = "Changed preset";
    h.update(s, 1.2);
    assert(h.count == 1);
}

static float edge(ImVec2 a, ImVec2 b, ImVec2 c)
{
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// Small CPU renderer for ImGui triangles, including the real font atlas and clipping.
static void save_frame(const char* path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 9; pixels[i + 1] = 17; pixels[i + 2] = 25; pixels[i + 3] = 255;
    }
    const ImDrawData* data = ImGui::GetDrawData();
    for (const ImDrawList* list : data->CmdLists)
    for (const ImDrawCmd& cmd : list->CmdBuffer)
    {
        if (cmd.UserCallback) continue;
        const ImTextureData* texture = cmd.TexRef._TexData;
        assert(texture && texture->Pixels);
        for (unsigned i = 0; i < cmd.ElemCount; i += 3)
        {
            const ImDrawVert& a = list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i]];
            const ImDrawVert& b = list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i + 1]];
            const ImDrawVert& c = list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i + 2]];
            const float area = edge(a.pos, b.pos, c.pos);
            if (std::fabs(area) < 0.00001f) continue;
            int left = std::max(0, static_cast<int>(std::max(cmd.ClipRect.x, std::floor(std::min({a.pos.x, b.pos.x, c.pos.x})))));
            int top = std::max(0, static_cast<int>(std::max(cmd.ClipRect.y, std::floor(std::min({a.pos.y, b.pos.y, c.pos.y})))));
            int right = std::min(width, static_cast<int>(std::min(cmd.ClipRect.z, std::ceil(std::max({a.pos.x, b.pos.x, c.pos.x})))));
            int bottom = std::min(height, static_cast<int>(std::min(cmd.ClipRect.w, std::ceil(std::max({a.pos.y, b.pos.y, c.pos.y})))));
            for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
            {
                ImVec2 point(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                float wa = edge(b.pos, c.pos, point) / area;
                float wb = edge(c.pos, a.pos, point) / area;
                float wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                float u = a.uv.x * wa + b.uv.x * wb + c.uv.x * wc;
                float v = a.uv.y * wa + b.uv.y * wb + c.uv.y * wc;
                int tx = std::clamp(static_cast<int>(u * texture->Width), 0, texture->Width - 1);
                int ty = std::clamp(static_cast<int>(v * texture->Height), 0, texture->Height - 1);
                const auto* texel = texture->Pixels + (ty * texture->Width + tx) * texture->BytesPerPixel;
                float alpha = (((a.col >> 24) & 255) * wa + ((b.col >> 24) & 255) * wb + ((c.col >> 24) & 255) * wc) / 255;
                alpha *= texel[texture->BytesPerPixel == 4 ? 3 : 0] / 255.0f;
                size_t index = (static_cast<size_t>(y) * width + x) * 4;
                for (int channel = 0; channel < 3; ++channel)
                {
                    int shift = channel * 8;
                    float color = ((a.col >> shift) & 255) * wa + ((b.col >> shift) & 255) * wb + ((c.col >> shift) & 255) * wc;
                    pixels[index + channel] = static_cast<unsigned char>(color * alpha + pixels[index + channel] * (1 - alpha));
                }
            }
        }
    }
    FIBITMAP* bitmap = FreeImage_ConvertFromRawBits(pixels.data(), width, height, width * 4, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, true);
    assert(bitmap);
    // FreeImage's native little-endian storage is BGRA.
    for (unsigned y = 0; y < FreeImage_GetHeight(bitmap); ++y)
    {
        BYTE* row = FreeImage_GetScanLine(bitmap, y);
        for (int x = 0; x < width; ++x) std::swap(row[x * 4], row[x * 4 + 2]);
    }
    assert(FreeImage_Save(FIF_PNG, bitmap, path));
    FreeImage_Unload(bitmap);
}

int main(int argc, char** argv)
{
    FreeImage_Initialise();
    check_history();
    const float scale = argc > 2 ? std::stof(argv[2]) : 1.0f;
    const bool warnings = argc > 3;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(1200 * scale + 32, 700 * scale + 32);
    io.Fonts->AddFontFromFileTTF("data/fonts/Inter/Inter-Regular.ttf", 14);
    snapshot s;
    s.vehicle_id = 1; s.name = "Ferrari LaFerrari"; s.gear = "4"; s.differential = "LSD";
    s.speed = 184; s.rpm = 6820; s.redline = 9000; s.engine_running = true;
    s.torque = 612; s.motor_kw = 95; s.clutch = 1; s.throttle = 0.72f;
    s.lateral_g = 0.82f; s.longitudinal_g = 0.34f; s.steering = 0.18f;
    s.hybrid = true; s.battery_soc = 0.76f; s.battery_temp = 38; s.battery_kw = 102;
    s.abs_enabled = s.tc_enabled = s.drs_enabled = s.aero_valid = true;
    s.front_downforce = 650; s.rear_downforce = 1100; s.drag = 940; s.ride_height = 0.116f; s.distance = 7340;
    for (int i = 0; i < 4; ++i)
    {
        corner& w = s.wheels[i];
        w.grounded = true; w.core = 78 + i * 3.0f; w.pressure = 2.3f; w.wear = 0.08f;
        w.load = 2700 + i * 310.0f; w.saturation = 0.45f + i * 0.1f;
        w.compression = 0.3f + i * 0.1f; w.slip_angle = -2.3f; w.slip_ratio = 0.02f;
        w.brake_temp = 320 + i * 40.0f; w.road = "Asphalt";
        w.surface[0] = 88; w.surface[1] = 84; w.surface[2] = 79;
    }
    if (warnings)
    {
        s.wheels[0].grounded = false; s.wheels[0].load = 0; s.wheels[0].saturation = 0;
        s.wheels[1].abs = true; s.wheels[2].saturation = 1; s.wheels[2].wear = 0.85f;
        s.wheels[3].brake_efficiency = 0.65f; s.wheels[3].brake_temp = 850;
        s.tc_active = true; s.tc_reduction = 0.35f; s.limiter = true; s.hybrid = false; s.aero_valid = false;
    }
    if (argc > 3 && std::string(argv[3]) == "cheap") s.full_simulation = false;
    history h;
    for (int i = 0; i <= 240; ++i)
    {
        snapshot v = s;
        v.speed = 100 + i * 0.35f + std::sin(i * 0.05f) * 10;
        v.throttle = unit(0.5f + std::sin(i * 0.045f) * 0.5f);
        v.brake = unit(-std::sin(i * 0.045f) * 0.5f);
        v.lateral_g = std::sin(i * 0.015f); v.longitudinal_g = std::cos(i * 0.02f) * 0.6f;
        for (int w = 0; w < 4; ++w) v.wheels[w].compression += std::sin(i * 0.16f + w) * 0.13f;
        h.update(v, i / 30.0);
    }
    ImGui::NewFrame();
    draw({ImGui::GetBackgroundDrawList(), ImVec2(16, 16), scale}, s, h, 8);
    ImGui::Render();
    save_frame(argc > 1 ? argv[1] : "binaries/car_tests/telemetry.png", static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    ImGui::DestroyContext();
    FreeImage_DeInitialise();
    std::puts("Telemetry history checks and production dashboard render passed.");
}
