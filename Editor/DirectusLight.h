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
#include "DirectusComboSliderText.h"
#include <QPushButton>
#include "Core/GameObject.h"
#include <QDoubleValidator>
#include "Components/Light.h"
#include <QComboBox>
#include "DirectusCore.h"
#include <QLabel>
#include "Math/Vector4.h"
//==================================

class DirectusLight : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusLight(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore);
    void Reflect(GameObject* gameobject);
private:

    //= TITLE ============================
    QLabel* m_title;
    //====================================

    //= LIGHT TYPE =======================
    QLabel* m_lightTypeLabel;
    QComboBox* m_lightType;
    //====================================

    //= COLOR ============================
    QLabel* m_colorLabel;
    QPushButton* m_color;
    //====================================

    //= INTENSTITY =======================
    QLabel* m_intensityLabel;
    DirectusComboSliderText* m_intensity;
    //====================================

    //= SHADOW TYPE ======================
    QLabel* m_shadowTypeLabel;
    QComboBox* m_shadowType;
    //====================================

    //= LINE ========================
    QWidget* m_line;
    //===============================

    //= MISC =============================
    QGridLayout* m_gridLayout;
    QValidator* m_validator;
    Light* m_inspectedLight;
    DirectusCore* m_directusCore;
    //====================================

    QLineEdit* CreateQLineEdit();

    void SetLightType(LightType type);
    void SetColor(Directus::Math::Vector4 color);
    void SetIntensity(float intensity);
    void SetShadowType(ShadowType type);

public slots:
    void MapLightType();
    void MapColor();
    void MapIntensity();
    void MapShadowType();
};
