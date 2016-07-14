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

//========================
#include "DirectusLight.h"
//========================

DirectusLight::DirectusLight(QWidget *parent) : QWidget(parent)
{

}

void DirectusLight::Initialize()
{
    m_gridLayout = new QGridLayout();
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

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ======================================================================
    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 3);
    //==============================================================================

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

    // Do the actual mapping
    //SetProjection(m_inspectedCamera->GetProjection());
    //SetFOV(m_inspectedCamera->GetFieldOfView());
    //SetNearPlane(m_inspectedCamera->GetNearPlane());
    //SetFarPlane(m_inspectedCamera->GetFarPlane());

    // Make this widget visible
    this->show();
}

void DirectusLight::Map()
{

}
