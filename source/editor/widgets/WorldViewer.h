/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
#include <memory>
//=================

namespace spartan { class Entity; }

class WorldViewer : public Widget
{
public:
    WorldViewer(Editor* editor);

    void OnTickVisible() override;
    void SetSelectedEntity(spartan::Entity* entity);

private:
    // tree
    void TreeShow();
    void OnTreeBegin();
    void OnTreeEnd();
    void TreeAddEntity(spartan::Entity* entity);
    void DrawToolbar();
    void HandleClicking();

    // misc
    void Popups();
    void PopupContextMenu() const;
    void HandleKeyShortcuts();

    // context menu actions
    static void ActionEntityDelete(spartan::Entity* entity);
    static spartan::Entity* ActionEntityCreateEmpty();
    static void ActionEntityCreateCube();
    static void ActionEntityCreateQuad();
    static void ActionEntityCreateSphere();
    static void ActionEntityCreateCylinder();
    static void ActionEntityCreateCone();
    static void ActionEntityCreateCamera();
    static void ActionEntityCreateTerrain();
    static void ActionEntityCreateLightDirectional();
    static void ActionEntityCreateLightPoint();
    static void ActionEntityCreateLightSpot();
    static void ActionEntityCreateLightArea();
    static void ActionEntityCreatePhysicsBody();
    static void ActionEntityCreateAudioSource();
};
