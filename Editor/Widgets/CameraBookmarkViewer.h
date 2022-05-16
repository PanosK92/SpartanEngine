#pragma once
#include "Widget.h"
#include "World/World.h"
#include "Math/Vector3.h"

class CameraBookmarkViewer : public Widget
{
public:
    CameraBookmarkViewer(Editor* editor);
    void TickVisible() override;

private:
    void ShowBookmarks();
    void ShowAddBookmarkButton();
    void AddCameraBookmark(Spartan::CameraBookmark bookmark);

    void ShowGoToBookmarkButton(int bookmarkIndex);
    void GoToBookmark(int bookmarkIndex);
};
