/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <string>
#include <vector>
#include "world/World.h"

class Editor;

class WorldSelector
{
public:
    static void Initialize(Editor* editor);
    static void Tick();

    static bool GetVisible();
    static void SetVisible(bool visibility);
    static void ShowDownloadPromptIfNeeded();

    static const std::vector<spartan::WorldMetadata>& GetWorlds();
    static bool IsRecent(const std::string& file_path);
    static bool OpenWorld(const std::string& file_path);
};
