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

//= INCLUDES ==================
#include <vector>
#include <memory>
#include "RHI/RHI_Definition.h"
#include "Widgets/Widget.h"
//=============================

//= FORWARD DECLARATIONS =
namespace Spartan 
{
	class Context; 
	class Engine;
	class Renderer;
    class Profiler;
    struct WindowData;
}
//========================

class Editor
{
public:
    Editor() = default;
	~Editor();

    void OnWindowMessage(Spartan::WindowData& window_data);
	void OnTick();

private:
	void Widgets_Create();
	void Widgets_Tick();
	void DockSpace_Begin();
	void DockSpace_End();	
	void ApplyStyle() const;

	// Editor
	std::vector<std::unique_ptr<Widget>> m_widgets;
	bool m_initializing = false;

	// Engine
	std::unique_ptr<Spartan::Engine> m_engine;
	std::shared_ptr<Spartan::RHI_Device> m_rhi_device;
	Spartan::Context* m_context	    = nullptr;
	Spartan::Renderer* m_renderer	= nullptr;
    Spartan::Profiler* m_profiler   = nullptr;
};
