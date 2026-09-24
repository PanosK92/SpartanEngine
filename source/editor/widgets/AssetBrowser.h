/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
//=================

class AssetBrowser : public Widget
{
public:
    AssetBrowser(Editor* editor);

    void OnTickVisible() override;
    void ShowMeshImportDialog(const std::string& file_path);

private:
    void OnPathClicked(const std::string& path) const;
};
