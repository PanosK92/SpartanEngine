/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
#include <optional>
//=================

// lets the mcp drive the browser, requests are applied on the next frame of the widget
struct AssetBrowserRequest
{
    std::optional<std::string> path;
    std::optional<std::string> view;
    std::optional<std::string> search;
    std::optional<std::string> kind;
    std::optional<std::string> select;
    std::optional<float> size;
};

struct AssetBrowserState
{
    std::string path;
    std::string view;
    float size = 0.0f;
    std::string kind;
    std::string selected;
    std::vector<std::string> visible;
};

class AssetBrowser : public Widget
{
public:
    AssetBrowser(Editor* editor);

    void OnTickVisible() override;
    void ShowMeshImportDialog(const std::string& file_path);

    static void Request(const AssetBrowserRequest& request);
    static AssetBrowserState GetState();

private:
    void OnPathClicked(const std::string& path) const;
};
