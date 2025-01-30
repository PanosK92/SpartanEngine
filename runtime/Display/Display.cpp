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

//= INCLUDES ===================
#include "pch.h"
#include "Display.h"
#include <SDL.h>
#include "Window.h"
#if defined(_WIN32)
#include <dxgi.h>
#include <dxgi1_6.h>
#include <wrl.h>
#pragma comment(lib, "dxgi.lib")
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
#endif
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        vector<DisplayMode> display_modes;
        bool is_hdr_capable      = false;
        float gamma              = 2.2f;
        float luminance_nits_max = 0;
        float luminance_nits_min = 0;

        void get_hdr_capabilities(bool* is_hdr_capable, float* luminance_min, float* luminance_max)
        {
            *is_hdr_capable = false;
            *luminance_min  = 0.0f;
            *luminance_max  = 0.0f;

            #if defined(_WIN32)
                // create dxgi factory
                Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
                if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                {
                    SP_LOG_ERROR("Failed to create DXGI factory");
                    return;
                }

                // enumerate and get the primary adapter (gpu)
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                for (UINT i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adapter); ++i)
                {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);
                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    {
                        continue;
                    }

                    break;
                }

                if (!adapter)
                {
                    SP_LOG_ERROR("No DXGI adapter found");
                    return;
                }

                // find primary display by detecting which display is being intersected the most by the engine window
                Microsoft::WRL::ComPtr<IDXGIOutput> output_primary;
                {
                    UINT i = 0;
                    Microsoft::WRL::ComPtr<IDXGIOutput> output_current;
                    float best_intersection_area = -1;
                    RECT window_rect;
                    GetWindowRect(static_cast<HWND>(Window::GetHandleRaw()), &window_rect);
                    while (adapter->EnumOutputs(i, &output_current) != DXGI_ERROR_NOT_FOUND)
                    {
                        // get the rectangle bounds of the app window
                        int ax1 = window_rect.left;
                        int ay1 = window_rect.top;
                        int ax2 = window_rect.right;
                        int ay2 = window_rect.bottom;

                        // get the rectangle bounds of current output
                        DXGI_OUTPUT_DESC desc;
                        if (FAILED(output_current->GetDesc(&desc)))
                        {
                            SP_LOG_ERROR("Failed to get output description");
                            return;
                        }

                        RECT r  = desc.DesktopCoordinates;
                        int bx1 = r.left;
                        int by1 = r.top;
                        int bx2 = r.right;
                        int by2 = r.bottom;

                        // compute the intersection
                        int intersectArea = max(0, min(ax2, bx2) - max(ax1, bx1)) * max(0, min(ay2, by2) - max(ay1, by1));
                        if (intersectArea > best_intersection_area)
                        {
                            output_primary = output_current;
                            best_intersection_area = static_cast<float>(intersectArea);
                        }

                        i++;
                    }
                }

                // get display capabilities
                Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
                if (SUCCEEDED(output_primary.As(&output6)))
                {
                    DXGI_OUTPUT_DESC1 desc;
                    if (SUCCEEDED(output6->GetDesc1(&desc)))
                    {
                        *is_hdr_capable = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                        *luminance_min  = desc.MinLuminance;
                        *luminance_max  = desc.MaxLuminance;
                    }
                }
            #else
                SP_ASSERT_MSG(false, "HDR support detection not implemented");
            #endif
        }

        void get_gamma(float* gamma)
        {
            *gamma = 2.2f;
        
        #ifdef _WIN32
            HDC hdc = GetDC(nullptr); // get the device context for the primary monitor
            if (!hdc)
            {
                SP_LOG_ERROR("Failed to get device context");
                return;
            }
        
            WORD gammaRamp[3][256];
            if (GetDeviceGammaRamp(hdc, gammaRamp))
            {
                // normalize the gamma ramp values and calculate the gamma value
                float sum = 0.0f;
                for (int i = 0; i < 256; ++i)
                {
                    // normalize the red channel value to [0, 1]
                    float normalizedValue = static_cast<float>(gammaRamp[0][i]) / 65535.0f;
                    // accumulate the normalized value
                    sum += normalizedValue;
                }

                // calculate the average normalized value
                float averageValue = sum / 256.0f;
                // estimate gamma as the inverse of the average value
                *gamma = 1.0f / averageValue;
            }
            else
            {
                SP_LOG_ERROR("Failed to get gamma ramp");
            }
        
            ReleaseDC(nullptr, hdc);
        
        #elif defined(__linux__)
            auto* display = XOpenDisplay(nullptr);
            if (!display)
            {
                SP_LOG_ERROR("Failed to open X display");
                return gamma;
            }
        
            XF86VidModeGamma gammaRamp;
            if (XF86VidModeGetGamma(display, DefaultScreen(display), &gammaRamp))
            {
                // normalize the gamma ramp values and calculate the gamma value
                float sum = 0.0f;
                for (int i = 0; i < 256; ++i)
                {
                    // normalize the red channel value to [0, 1]
                    float normalizedValue = static_cast<float>(gammaRamp.red[i]) / 65535.0f;
                    // accumulate the normalized value
                    sum += normalizedValue;
                }
                // calculate the average normalized value
                float averageValue = sum / 256.0f;
                // estimate gamma as the inverse of the average value
                *gamma = 1.0f / averageValue;
            }
            else
            {
                SP_LOG_ERROR("Failed to get gamma ramp");
            }
        
            XCloseDisplay(display);
        #endif
        }
    }

    void Display::RegisterDisplayMode(const uint32_t width, const uint32_t height, uint32_t hz, uint8_t display_index)
    {
        SP_ASSERT_MSG(width  != 0,    "width can't be zero");
        SP_ASSERT_MSG(height != 0,    "height can't be zero");
        SP_ASSERT_MSG(hz     != 0.0f, "hz can't be zero");

        // Early exit if the display mode is already registered
        for (const DisplayMode& display_mode : display_modes)
        {
            if (display_mode.width         == width  &&
                display_mode.height        == height &&
                display_mode.hz            == hz     &&
                display_mode.display_index == display_index)
                return;
        }

        // add the new display mode
        display_modes.emplace_back(width, height, hz, display_index);

        // sort display modes based on width, descending order
        sort(display_modes.begin(), display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.width > display_mode_b.width;
        });
    }

    void Display::Initialize()
    {
        display_modes.clear();

        // get display index of the display that contains this window
        int display_index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));
        if (display_index < 0)
        {
            SP_LOG_ERROR("Failed to window display index");
            return;
        }

        // get display mode count
        int display_mode_count = SDL_GetNumDisplayModes(display_index);
        if (display_mode_count <= 0)
        {
            SP_LOG_ERROR("Failed to get display mode count");
            return;
        }

        // register display modes
        for (int display_mode_index = 0; display_mode_index < display_mode_count; display_mode_index++)
        {
            SDL_DisplayMode display_mode;
            if (SDL_GetDisplayMode(display_index, display_mode_index, &display_mode) == 0)
            {
                RegisterDisplayMode(display_mode.w, display_mode.h, display_mode.refresh_rate, display_index);
            }
            else
            {
                SP_LOG_ERROR("Failed to get display mode %d for display %d", display_mode_index, display_index);
            }
        }

        // detect hdr capabilities
        get_gamma(&gamma);
        get_hdr_capabilities(&is_hdr_capable, &luminance_nits_min, &luminance_nits_max);
        SP_LOG_INFO("HDR: %s, min luminance: %.0f nits, max luminance: %.0f nits", is_hdr_capable ? "true" : "false", luminance_nits_min, luminance_nits_max);
    }

    const vector<DisplayMode>& Display::GetDisplayModes()
    {
        return display_modes;
    }
    
    uint32_t Display::GetWidth()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);

        return display_mode.w;
    }

    uint32_t Display::GetHeight()
    {
        int display_index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));

        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);

        return display_mode.h;
    }

    uint32_t Display::GetRefreshRate()
    {
        SDL_DisplayMode display_mode;
        SP_ASSERT(SDL_GetCurrentDisplayMode(GetIndex(), &display_mode) == 0);
       
        return display_mode.refresh_rate;
    }

    uint32_t Display::GetIndex()
    {
        int index = SDL_GetWindowDisplayIndex(static_cast<SDL_Window*>(Window::GetHandleSDL()));

        // during engine startup, the window doesn't exist yet, therefore it's not displayed by any monitor.
        // in this case the index can be -1, so we'll instead set the index to 0 (whatever the primary display is)
        return index != -1 ? index : 0;
    }

    bool Display::GetHdr()
    {
        return is_hdr_capable;
    }

    float Display::GetLuminanceMax()
    {
        return luminance_nits_max;
    }

    float Display::GetGamma()
    {
        return gamma;
    }

    const char* Display::GetName()
    {
        return SDL_GetDisplayName(GetIndex());
    }
}
