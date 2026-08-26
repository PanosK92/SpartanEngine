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

//= INCLUDES ========================
#include "pch.h"
#include "Editor.h"
#include "widgets/Style.h"
#include "widgets/ProgressDialog.h"
#include "widgets/Viewport.h"
#include "widgets/WorldViewer.h"
#include "widgets/Properties.h"
#include "widgets/Console.h"
#include "widgets/AssetBrowser.h"
#include "widgets/Sequencer.h"
#include "widgets/Profiler.h"
#include "widgets/MemoryViewer.h"
#include "widgets/ResourceViewer.h"
#include "widgets/ShaderEditor.h"
#include "widgets/ScriptEditor.h"
#include "widgets/TerrainEditor.h"
#include "widgets/RenderOptions.h"
#include "widgets/TextureViewer.h"
#include "widgets/AssetViewer.h"
#include "mcp/McpAssistant.h"
//===================================

void Editor::RegisterWidgets()
{
    AddWidget<Style>();
    AddWidget<ProgressDialog>();
    AddWidget<Viewport>();
    AddWidget<WorldViewer>();
    AddWidget<Properties>();
    AddWidget<Console>();
    AddWidget<AssetBrowser>();
    AddWidget<Sequencer>();
    AddWidget<Profiler>();
    AddWidget<MemoryViewer>();
    AddWidget<ResourceViewer>();
    AddWidget<ShaderEditor>();
    AddWidget<ScriptEditor>();
    AddWidget<TerrainEditor>();
    AddWidget<RenderOptions>();
    AddWidget<TextureViewer>();
    AddWidget<AssetViewer>();
    AddWidget<McpAssistant>();
}
