/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

class Editor;

class GeneralWindows
{
public:
    static void Initialize(Editor* editor);
    static void Tick();

    static bool GetVisibilityWorlds();
    static void SetVisibilityWorlds(const bool visibility);

    static bool* GetVisibilityWindowAbout();
    static bool* GetVisibilityWindowControls();
};
