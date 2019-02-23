/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==============
#include "RHI_Object.h"
#include "RHI_Definition.h"
#include <memory>
//=========================

namespace Directus
{
	class ENGINE_CLASS RHI_RasterizerState : public RHI_Object
	{
	public:
		RHI_RasterizerState(
			std::shared_ptr<RHI_Device> device,
			RHI_Cull_Mode cullMode,
			RHI_Fill_Mode fillMode,
			bool depthClipEnabled,
			bool scissorEnabled,
			bool multiSampleEnabled, 
			bool antialisedLineEnabled
		);
		~RHI_RasterizerState();

		RHI_Cull_Mode GetCullMode()		{ return m_cullMode; }
		RHI_Fill_Mode GetFillMode()		{ return m_fillMode; }
		bool GetDepthClipEnabled()		{ return m_depthClipEnabled; }
		bool GetScissorEnabled()		{ return m_scissorEnabled; }
		bool GetMultiSampleEnabled()	{ return m_multiSampleEnabled; }
		bool GetAntialisedLineEnabled()	{ return m_antialisedLineEnabled; }
		bool IsInitialized()			{ return m_initialized; }
		void* GetBuffer()				{ return m_buffer; }

	private:
		// Properties
		RHI_Cull_Mode m_cullMode;
		RHI_Fill_Mode m_fillMode;
		bool m_depthClipEnabled;
		bool m_scissorEnabled;
		bool m_multiSampleEnabled;
		bool m_antialisedLineEnabled;

		// Initialized
		bool m_initialized = false;

		// Rasterizer state view
		void* m_buffer = nullptr;
	};
}