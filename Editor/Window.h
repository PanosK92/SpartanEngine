#pragma once

//= INCLUDES ===============
#include <string>
#include <Windows.h>
#include <functional>
#include "Core/Engine.h"
#include "Core/FileSystem.h"
//==========================

namespace Window
{
    static HINSTANCE g_instance;
    static HWND g_handle;
    static std::function<void(Spartan::WindowData& window_data)> g_on_message;

    inline void GetWindowSize(float* width, float* height)
    {
        RECT rect;
        ::GetClientRect(g_handle, &rect);
        *width  = static_cast<float>(rect.right - rect.left);
        *height = static_cast<float>(rect.bottom - rect.top);
    }

    inline uint32_t GetWidth()
    {
        RECT rect;
        GetClientRect(g_handle, &rect);
        return static_cast<uint32_t>(rect.right - rect.left);
    }

    inline uint32_t GetHeight()
    {
        RECT rect;
        GetClientRect(g_handle, &rect);
        return static_cast<uint32_t>(rect.bottom - rect.top);
    }

    // Window Procedure
    inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;

        Spartan::WindowData window_data     = {};
        window_data.handle                  = static_cast<void*>(g_handle);
        window_data.instance                = static_cast<void*>(g_instance);
        window_data.message                 = static_cast<uint32_t>(msg);
        window_data.wparam                  = static_cast<uint64_t>(wParam);
        window_data.lparam                  = static_cast<int64_t>(lParam);
        window_data.monitor_width           = GetSystemMetrics(SM_CXSCREEN);
        window_data.monitor_height          = GetSystemMetrics(SM_CYSCREEN);
        window_data.monitor_width_virtual   = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        window_data.monitor_height_virtual  = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        GetWindowSize(&window_data.width, &window_data.height);

        if (msg == WM_DISPLAYCHANGE || msg == WM_SIZE)
        { 
            window_data.width   = static_cast<float>(lParam & 0xffff);
            window_data.height  = static_cast<float>((lParam >> 16) & 0xffff);
        }
        else if (msg == WM_CLOSE)
        {
            PostQuitMessage(0);
        }
        else
        {
            result = DefWindowProc(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_SYSCOMMAND)
        {
            window_data.minimise = wParam == SC_MINIMIZE;
            window_data.maximise = wParam == SC_MAXIMIZE;
        }

        if (g_on_message)
        {
            g_on_message(window_data);
        }

        return result;
    }

    inline bool Create(HINSTANCE instance, const std::string& title)
    {
        g_instance = instance;
        const std::wstring windowTitle    = Spartan::FileSystem::StringToWstring(title);
        const int windowWidth            = GetSystemMetrics(SM_CXSCREEN);
        const int windowHeight            = GetSystemMetrics(SM_CYSCREEN);
        const LPCWSTR className            = L"myWindowClass";
    
        // Register the Window Class
        WNDCLASSEX wc;
        wc.cbSize        = sizeof(WNDCLASSEX);
        wc.style         = 0;
        wc.lpfnWndProc   = WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = g_instance;
        wc.hIcon         = LoadIcon(instance, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(instance, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = className;
        wc.hIconSm       = LoadIcon(instance, IDI_APPLICATION);
    
        if(!RegisterClassEx(&wc))
        {
            MessageBox(nullptr, L"Window registration failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
            return false;
        }
    
        // Create the Window
        g_handle = CreateWindowEx
        (
            WS_EX_CLIENTEDGE,
            className,
            windowTitle.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
            nullptr, nullptr, g_instance, nullptr
        );
    
        if(!g_handle)
        {
            MessageBox(nullptr, L"Window creation failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
            return false;
        }

        return true;
    }

    inline void Show()
    {
        ShowWindow(g_handle, SW_MAXIMIZE);
        UpdateWindow(g_handle);
        SetFocus(g_handle);
    }

    inline bool Tick()
    {
        MSG msg;
        ZeroMemory(&msg, sizeof(msg));

        while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                return false;
        }

        return true;
    }

    inline void Destroy()
    {
        DestroyWindow(g_handle);
    }
}
