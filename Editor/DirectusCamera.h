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
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include <QComboBox>
#include "Components/Camera.h"
#include <QPushButton>
#include "DirectusSliderText.h"
#include "DirectusCore.h"
//==============================

class DirectusCamera : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusCamera(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE =======================
    QLabel* m_title;
    //===============================

    //= BACKGROUND =================================
    QLabel* m_backgroundLabel;
    QPushButton* m_background;
    //==============================================

    //= PROJECTION =================================
    QLabel* m_projectionLabel;
    QComboBox* m_projectionComboBox;
    //==============================================

    //= FOV ========================================
    QLabel* m_fovLabel;
    DirectusSliderText* m_fov;
    //==============================================

    //= CLIPPING PLANES ============================
    QLabel* m_clippingPlanesLabel;
    DirectusAdjustLabel* m_clippingPlanesFarLabel;
    DirectusAdjustLabel* m_clippingPlanesNearLabel;
    QLineEdit* m_clippingNear;
    QLineEdit* m_clippingFar;
    //==============================================

    //= MISC ========================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Camera* m_inspectedCamera;
    DirectusCore* m_directusCore;
    //===============================

    QLineEdit* CreateQLineEdit();
    void SetProjection(Projection projection);
    void SetNearPlane(float nearPlane);
    void SetFarPlane(float farPlane);
    void SetFOV(float fov);

public slots:
    void MapProjection();
    void MapFOV();
    void MapClippingPlanes();
};
