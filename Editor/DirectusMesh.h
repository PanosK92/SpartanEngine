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

//==================================
#include <QWidget>
#include <QGridLayout>
#include "DirectusAdjustLabel.h"
#include <QLineEdit>
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "Components/Mesh.h"
#include <QCheckBox>
//==================================

class DirectusMesh : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusMesh(QWidget *parent = 0);
    void Initialize();
    void Reflect(GameObject* gameobject);
private:

    //= TITLE ============================
    QWidget* m_image;
    QLabel* m_title;
    //====================================

    //= CAST SHADOWS =====================
    QLabel* m_castShadowsLabel;
    QCheckBox* m_castShadowsCheckBox;
    //====================================

    //= RECEIVE SHADOWS ==================
    QLabel* m_receiveShadowsLabel;
    QCheckBox* m_receiveShadowsCheckBox;
    //====================================

    //= MATERIAL =========================
    QLabel* m_materialLabel;
    QLineEdit* m_material;
    //====================================

    //= MISC =============================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Mesh* m_inspectedMeshRenderer;
    //====================================

    QLineEdit* CreateQLineEdit();

    void SetCastShadows(bool cast);
    void SetReceiveShadows(bool receive);

public slots:
    void Map();
};
