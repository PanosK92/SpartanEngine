#pragma once

//= INCLUDES =====================
#include <string>
#include <Windows.h>
#include <functional>
#include "FileSystem/FileSystem.h"
//================================

namespace Window
{
	static HINSTANCE g_instance;
	static HWND g_handle;
	static std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)> g_OnMessage;
	static std::function<void(float, float)> g_onResize;

    inline void GetWindowSize(float* width, float* height)
    {
        RECT rect;
        ::GetClientRect(g_handle, &rect);
        *width  = static_cast<float>(rect.right - rect.left);
        *height = static_cast<float>(rect.bottom - rect.top);
    }

	// Window Procedure
	inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		g_OnMessage(hwnd, msg, wParam, lParam);

        auto resize_event = [&lParam]()
        {
            if (g_onResize) g_onResize(
                static_cast<float>(lParam & 0xffff),
                static_cast<float>((lParam >> 16) & 0xffff)
            );
        };

		switch(msg)
		{
			case WM_DISPLAYCHANGE:
                resize_event();
			    break;

			case WM_SIZE:
                resize_event();
                break;

			case WM_CLOSE:
				PostQuitMessage(0);	
			    break;
        
    		default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		return 0;
	}

	inline bool Create(HINSTANCE instance, const std::string& title)
	{
		g_instance = instance;
		std::wstring windowTitle	= Spartan::FileSystem::StringToWstring(title);
		int windowWidth				= GetSystemMetrics(SM_CXSCREEN);
		int windowHeight			= GetSystemMetrics(SM_CYSCREEN);
		LPCWSTR className			= L"myWindowClass";
	
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
		if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return msg.message != WM_QUIT;
	}

	inline int GetWidth()
	{
		RECT rect;
		GetClientRect(g_handle, &rect);
		return (int)(rect.right - rect.left);
	}

	inline int GetHeight()
	{
		RECT rect;
		GetClientRect(g_handle, &rect);
		return (int)(rect.bottom - rect.top);
	}

	inline void Destroy()
	{
		DestroyWindow(g_handle);
	}
}
