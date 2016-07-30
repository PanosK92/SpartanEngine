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
#include <QLineEdit>
#include "Core/GameObject.h"
#include <QComboBox>
#include "Components/Camera.h"
#include <QPushButton>
#include "DirectusComboSliderText.h"
#include "DirectusComboLabelText.h"
#include "DirectusColorPicker.h"
#include "DirectusCore.h"
#include <QToolButton>
//==================================

class DirectusCamera : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusCamera(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore, QWidget* mainWindow);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE =======================
    QLabel* m_title;
    QToolButton* m_optionsButton;
    //===============================

    //= BACKGROUND =================================
    QLabel* m_backgroundLabel;
    DirectusColorPicker* m_background;
    //==============================================

    //= PROJECTION =================================
    QLabel* m_projectionLabel;
    QComboBox* m_projectionComboBox;
    //==============================================

    //= FOV ========================================
    QLabel* m_fovLabel;
    DirectusComboSliderText* m_fov;
    //==============================================

    //= CLIPPING PLANES ============================
    QLabel* m_clippingPlanesLabel;
    DirectusComboLabelText* m_nearPlane;
    DirectusComboLabelText* m_farPlane;
    //==============================================

    //= LINE ========================
    QWidget* m_line;
    //===============================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    Camera* m_inspectedCamera;
    DirectusCore* m_directusCore;
    //===============================

    void ReflectBackground(Directus::Math::Vector4 color);
    void ReflectProjection(Projection projection);
    void ReflectNearPlane(float nearPlane);
    void ReflectFarPlane(float farPlane);
    void ReflectFOV(float fov);

public slots:
    void MapBackground();
    void MapProjection();
    void MapFOV();
    void MapNearPlane();
    void MapFarPlane();
};
