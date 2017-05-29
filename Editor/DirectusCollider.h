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
#include "DirectusIComponent.h"
//==================================

class DirectusCollider : public DirectusIComponent
{
    Q_OBJECT
public:
    DirectusCollider();

    virtual void Initialize(DirectusInspector* inspector, QWidget* mainWindow);
    virtual void Reflect(std::weak_ptr<Directus::GameObject> gameobject);

private:
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

    //= MISC =====================
    QValidator* m_validator;
    Directus::Collider* m_inspectedCollider;
    //============================

    void ReflectType();
    void ReflectCenter();
    void ReflectSize();

public slots:
    void MapType();
    void MapCenter();
    void MapSize();
    virtual void Remove();
};
