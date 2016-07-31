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

//=================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusComboLabelText.h"
#include <QLineEdit>
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "Components/Collider.h"
#include <QCheckBox>
#include "DirectusCore.h"
#include "QComboBox.h"
#include "DirectusDropDownButton.h"
//==================================

class DirectusInspector;

class DirectusCollider : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusCollider(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore, DirectusInspector* inspector, QWidget* mainWindow);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE ======================================
    QLabel* m_title;
    DirectusDropDownButton* m_optionsButton;
    //==============================================

    //= SHAPE TYPE =======================
    QLabel* m_shapeTypeLabel;
    QComboBox* m_shapeType;
    //====================================

    //= CENTER ===========================
    QLabel* m_centerLabel;
    DirectusComboLabelText* m_centerX;
    DirectusComboLabelText* m_centerY;
    DirectusComboLabelText* m_centerZ;
    //====================================

    //= SIZE =============================
    QLabel* m_sizeLabel;
    DirectusComboLabelText* m_sizeX;
    DirectusComboLabelText* m_sizeY;
    DirectusComboLabelText* m_sizeZ;
    //====================================

    //= LINE ========================
    QWidget* m_line;
    //===============================

    //= MISC =============================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Collider* m_inspectedCollider;
    DirectusCore* m_directusCore;
    DirectusInspector* m_inspector;
    //====================================

    void ReflectType(ColliderShape shape);
    void ReflectCenter(Directus::Math::Vector3 center);
    void ReflectSize(Directus::Math::Vector3 size);

public slots:
    void MapType();
    void MapCenter();
    void MapSize();
    void Remove();
};
