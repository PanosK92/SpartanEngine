/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

// FWD DECLARATIONS =
class Editor;
//===================

class MenuBar
{
public:
    static void Initialize(Editor* editor);
    static void Tick();

    static void ShowWorldSaveDialog();
    static void ShowWorldLoadDialog();

    static float GetPaddingX() { return 14.0f; }
    static float GetPaddingY() { return 10.0f; }
};
