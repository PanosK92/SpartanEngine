/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
