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

//= INCLUDES =================
#include "DirectusLight.h"
#include "DirectusInspector.h"
//============================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

DirectusLight::DirectusLight()
{

}

void DirectusLight::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;

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

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
    //=========================================================

    //= LIGHT TYPE ============================================
    m_lightTypeLabel = new QLabel("Type");
    m_lightType = new QComboBox();
    m_lightType->addItem("Directional");
    m_lightType->addItem("Point");
    //=========================================================

    //= RANGE ======================================
    m_range = new DirectusComboLabelText();
    m_range->Initialize("Range");
    m_range->AlignLabelToTheLeft();
    //=============================================

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
    m_gridLayout->addWidget(m_optionsButton, 0, 2, 1, 1, Qt::AlignRight);

    // Row 1 - LIGHT TYPE
    m_gridLayout->addWidget(m_lightTypeLabel,   1, 0, 1, 1);
    m_gridLayout->addWidget(m_lightType,        1, 1, 1, 2);

    // Row 2 - RANGE
    m_gridLayout->addWidget(m_range->GetLabelWidget(),  2, 0, 1, 1);
    m_gridLayout->addWidget(m_range->GetTextWidget(),   2, 1, 1, 2);

    // Row 3 - COLOR
    m_gridLayout->addWidget(m_colorLabel,           3, 0, 1, 1);
    m_gridLayout->addWidget(m_color->GetWidget(),   3, 1, 1, 2);

    // Row 4 - INTENSTITY
    m_gridLayout->addWidget(m_intensityLabel,               4, 0, 1, 1);
    m_gridLayout->addWidget(m_intensity->GetSlider(),       4, 1, 1, 1);
    m_gridLayout->addWidget(m_intensity->GetLineEdit(),     4, 2, 1, 1);

    // Row 5 - SHADOW TYPE
    m_gridLayout->addWidget(m_shadowTypeLabel,  5, 0, 1, 1);
    m_gridLayout->addWidget(m_shadowType,       5, 1, 1, 2);

    // Row 6 - LINE
    m_gridLayout->addWidget(m_line, 6, 0, 1, 3);
    //==============================================================================

    connect(m_optionsButton,        SIGNAL(Remove()),                   this, SLOT(Remove()));
    connect(m_lightType,    SIGNAL(currentIndexChanged(int)),   this, SLOT(MapLightType()));
    connect(m_range,        SIGNAL(ValueChanged()),             this, SLOT(MapRange()));
    connect(m_color,        SIGNAL(ColorPickingCompleted()),    this, SLOT(MapColor()));
    connect(m_intensity,    SIGNAL(ValueChanged()),             this, SLOT(MapIntensity()));
    connect(m_shadowType,   SIGNAL(currentIndexChanged(int)),   this, SLOT(MapShadowType()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusLight::Reflect(std::weak_ptr<Directus::GameObject> gameobject)
{
    m_inspectedLight = nullptr;

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedLight = gameobject.lock()->GetComponent<Light>();
    if (!m_inspectedLight)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectLightType();
    ReflectRange();
    ReflectColor();
    ReflectIntensity();
    ReflectShadowType();

    // Make this widget visible
    this->show();
}

void DirectusLight::ReflectLightType()
{
    if (!m_inspectedLight)
        return;

    LightType type = m_inspectedLight->GetLightType();
    m_lightType->setCurrentIndex((int)type);
}

void DirectusLight::ReflectRange()
{
    if (!m_inspectedLight)
        return;

    if (m_inspectedLight->GetLightType() == Point)
    {
        m_range->GetLabelWidget()->show();
        m_range->GetTextWidget()->show();
    }
    else
    {
        m_range->GetLabelWidget()->hide();
        m_range->GetTextWidget()->hide();
    }

    float range = m_inspectedLight->GetRange();
    m_range->SetFromFloat(range);
}

void DirectusLight::ReflectColor()
{
    if (!m_inspectedLight)
        return;

    Directus::Math::Vector4 color = m_inspectedLight->GetColor();
    m_color->SetColor(color);
}

void DirectusLight::ReflectIntensity()
{
    if (!m_inspectedLight)
        return;

    float intensity = m_inspectedLight->GetIntensity();
    m_intensity->SetValue(intensity);
}

void DirectusLight::ReflectShadowType()
{
    if (!m_inspectedLight)
        return;

    ShadowType type = m_inspectedLight->GetShadowType();
    m_shadowType->setCurrentIndex((int)type);
}

void DirectusLight::MapLightType()
{
    if(!m_inspectedLight)
        return;

    LightType type = (LightType)(m_lightType->currentIndex());
    m_inspectedLight->SetLightType(type);

    // It's important to reflect the range again because
    // setting a directional light to a point light, needs
    // a new field called Range.
    ReflectRange();
}

void DirectusLight::MapRange()
{
    if(!m_inspectedLight)
        return;

    float range = m_range->GetAsFloat();
    m_inspectedLight->SetRange(range);
}

void DirectusLight::MapColor()
{
    if(!m_inspectedLight)
        return;

    Vector4 color = m_color->GetColor();
    m_inspectedLight->SetColor(color);
}

void DirectusLight::MapIntensity()
{
    if(!m_inspectedLight)
        return;

    float intensity = m_intensity->GetValue();
    m_inspectedLight->SetIntensity(intensity);
}

void DirectusLight::MapShadowType()
{
    if(!m_inspectedLight)
        return;

    ShadowType type = (ShadowType)(m_shadowType->currentIndex());
    m_inspectedLight->SetShadowType(type);
}

void DirectusLight::Remove()
{
    if (!m_inspectedLight)
        return;

    auto gameObject = m_inspectedLight->g_gameObject;
    if (!gameObject.expired())
    {
        gameObject.lock()->RemoveComponent<Light>();
    }

    m_inspector->Inspect(gameObject);
}
