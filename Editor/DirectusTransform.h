/*
Copyright(c) 2016 Panos Karabelas

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

//==============================
#include <QWidget>
#include <QGridLayout>
#include "DirectusAdjustLabel.h"
#include <QLineEdit>
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "DirectusCore.h"
//==============================

class DirectusTransform : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusTransform(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore);

    void Reflect(GameObject* gameobject);
    Directus::Math::Vector3 GetPosition();
    void SetPosition(Directus::Math::Vector3 pos);

    Directus::Math::Quaternion GetRotation();
    void SetRotation(Directus::Math::Quaternion rot);
    void SetRotation(Directus::Math::Vector3 rot);

    Directus::Math::Vector3 GetScale();
    void SetScale(Directus::Math::Vector3 sca);

private:

    //= TITLE =======================
    QLabel* m_title;
    //===============================

    // = POSITION ===================
    QLabel* m_posLabel;
    DirectusAdjustLabel* m_posXLabel;
    QLineEdit* m_posX;
    DirectusAdjustLabel* m_posYLabel;
    QLineEdit* m_posY;
    DirectusAdjustLabel* m_posZLabel;
    QLineEdit* m_posZ;
    //===============================

    //= ROTATION ====================
    QLabel* m_rotLabel;
    DirectusAdjustLabel* m_rotXLabel;
    QLineEdit* m_rotX;
    DirectusAdjustLabel* m_rotYLabel;
    QLineEdit* m_rotY;
    DirectusAdjustLabel* m_rotZLabel;
    QLineEdit* m_rotZ;
    //===============================

    //= SCALE =======================
    QLabel* m_scaLabel;
    DirectusAdjustLabel* m_scaXLabel;
    QLineEdit* m_scaX;
    DirectusAdjustLabel* m_scaYLabel;
    QLineEdit* m_scaY;
    DirectusAdjustLabel* m_scaZLabel;
    QLineEdit* m_scaZ;
    //===============================

    //= LINE ========================
    QWidget* m_line;
    //===============================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    //===============================

    QLineEdit* CreateQLineEdit();
    Transform* m_inspectedTransform;
    DirectusCore* m_directusCore;

public slots:
    void MapPosition();
    void MapRotation();
    void MapScale();
};
