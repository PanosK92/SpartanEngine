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

//= INCLUDES =============
#include "DirectusLight.h"
//========================

//=============================
using namespace Directus::Math;
//=============================

DirectusLight::DirectusLight(QWidget *parent) : QWidget(parent)
{
    m_directusCore = nullptr;
}

void DirectusLight::Initialize(DirectusCore* directusCore, QWidget* mainWindow)
{
    m_directusCore = directusCore;
    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Light");
    m_title->setStyleSheet(
                "background-image: url(:/Images/light.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //=========================================================

    //= LIGHT TYPE ============================================
    m_lightTypeLabel = new QLabel("Type");
    m_lightType = new QComboBox();
    m_lightType->addItem("Directional");
    m_lightType->addItem("Point");
    //=========================================================

    //= COLOR ====================================
    m_colorLabel = new QLabel("Color");
    m_color = new DirectusColorPicker();
    m_color->Initialize(mainWindow);
    //=============================================

    //= INTENSTITY =======================
    m_intensityLabel = new QLabel("Intensity");
    m_intensity = new DirectusComboSliderText();
    m_intensity->Initialize(0, 8);
    //====================================

    //= SHADOW TYPE ======================
    m_shadowTypeLabel = new QLabel("Shadow type");
    m_shadowType = new QComboBox();
    m_shadowType->addItem("No Shadows");
    m_shadowType->addItem("Hard Shadows");
    m_shadowType->addItem("Soft Shadows");
    //====================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ======================================================================
    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);

    // Row 1 - LIGHT TYPE
    m_gridLayout->addWidget(m_lightTypeLabel,   1, 0, 1, 1);
    m_gridLayout->addWidget(m_lightType,        1, 1, 1, 2);

    // Row 2 - COLOR
    m_gridLayout->addWidget(m_colorLabel,   2, 0, 1, 1);
    m_gridLayout->addWidget(m_color->GetWidget(),        2, 1, 1, 2);

    // Row 3 - INTENSTITY
    m_gridLayout->addWidget(m_intensityLabel,               3, 0, 1, 1);
    m_gridLayout->addWidget(m_intensity->GetSlider(),       3, 1, 1, 1);
    m_gridLayout->addWidget(m_intensity->GetLineEdit(),     3, 2, 1, 1);

    // Row 4 - SHADOW TYPE
    m_gridLayout->addWidget(m_shadowTypeLabel,  4, 0, 1, 1);
    m_gridLayout->addWidget(m_shadowType,       4, 1, 1, 2);

    // Row 5 - LINE
    m_gridLayout->addWidget(m_line, 5, 0, 1, 3);
    //==============================================================================

    connect(m_lightType,    SIGNAL(currentIndexChanged(int)),   this, SLOT(MapLightType()));
    connect(m_color,        SIGNAL(ColorPickingCompleted()),    this, SLOT(MapColor()));
    connect(m_intensity,    SIGNAL(ValueChanged()),             this, SLOT(MapIntensity()));
    connect(m_shadowType,   SIGNAL(currentIndexChanged(int)),   this, SLOT(MapShadowType()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusLight::Reflect(GameObject* gameobject)
{
    m_inspectedLight = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedLight = gameobject->GetComponent<Light>();
    if (!m_inspectedLight)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectLightType(m_inspectedLight->GetLightType());
    ReflectColor(m_inspectedLight->GetColor());
    ReflectIntensity(m_inspectedLight->GetIntensity());
    ReflectShadowType(m_inspectedLight->GetShadowType());

    // Make this widget visible
    this->show();
}

void DirectusLight::ReflectLightType(LightType type)
{
    m_lightType->setCurrentIndex((int)type);
}

void DirectusLight::ReflectColor(Directus::Math::Vector4 color)
{
    m_color->SetColor(color);
}

void DirectusLight::ReflectIntensity(float intensity)
{
    m_intensity->SetValue(intensity);
}

void DirectusLight::ReflectShadowType(ShadowType type)
{
    m_shadowType->setCurrentIndex((int)type);
}

void DirectusLight::MapLightType()
{
    if(!m_inspectedLight || !m_directusCore)
        return;

    LightType type = (LightType)(m_lightType->currentIndex());
    m_inspectedLight->SetLightType(type);

    m_directusCore->Update();
}

void DirectusLight::MapColor()
{
    if(!m_inspectedLight || !m_directusCore)
        return;

    Vector4 color = m_color->GetColor();
    m_inspectedLight->SetColor(color);

    m_directusCore->Update();
}

void DirectusLight::MapIntensity()
{
    if(!m_inspectedLight || !m_directusCore)
        return;

    float intensity = m_intensity->GetValue();
    m_inspectedLight->SetIntensity(intensity);

    m_directusCore->Update();
}

void DirectusLight::MapShadowType()
{
    if(!m_inspectedLight || !m_directusCore)
        return;

    ShadowType type = (ShadowType)(m_shadowType->currentIndex());
    m_inspectedLight->SetShadowType(type);

    m_directusCore->Update();
}
