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

//= INCLUDES ====================
#include "DirectusMeshRenderer.h"
//===============================

DirectusMeshRenderer::DirectusMeshRenderer(QWidget *parent) : QWidget(parent)
{

}

void DirectusMeshRenderer::Initialize()
{
    m_gridLayout = new QGridLayout();
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_image = new QWidget(this);
    m_image->setStyleSheet("background-image: url(:/Images/meshRenderer.png); background-repeat: no-repeat; background-position: left;");
    m_title = new QLabel("Mesh Renderer");
    //=========================================================

    //= CAST SHADOWS ==========================================
    m_castShadowsLabel = new QLabel("Cast Shadows");
    m_castShadowsCheckBox = new QCheckBox();
    //=========================================================

    //= RECEIVE SHADOWS =======================================
    m_receiveShadowsLabel = new QLabel("Receive Shadows");
    m_receiveShadowsCheckBox = new QCheckBox();
    //=========================================================

    //= RECEIVE SHADOWS =======================================
    m_materialLabel = new QLabel("Material");
    m_material = new QLineEdit();
    //=========================================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    // Row 0
    m_gridLayout->addWidget(m_image, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_title, 0, 1, 1, 1);

    // Row 1
    m_gridLayout->addWidget(m_castShadowsLabel, 1, 0, 1, 1);
    m_gridLayout->addWidget(m_castShadowsCheckBox, 1, 1, 1, 1);

    // Row 2
    m_gridLayout->addWidget(m_receiveShadowsLabel, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_receiveShadowsCheckBox, 2, 1, 1, 1);

    // Row 3
    m_gridLayout->addWidget(m_materialLabel, 3, 0, 1, 1);
    m_gridLayout->addWidget(m_material, 3, 1, 1, 1);
    //============================================================

    // Connect textEdit(QString) signal with the appropriate slot
    // NOTE: Unlike textChanged(), this signal is not emitted when the
    // text is changed programmatically, for example, by calling setText().
    //connect(m_posX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_rotX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_scaX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMeshRenderer::Map(GameObject* gameobject)
{
    m_inspectedMeshRenderer = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedMeshRenderer = gameobject->GetComponent<MeshRenderer>();
    if (!m_inspectedMeshRenderer)
    {
        this->hide();
        return;
    }

    // Do the actual mapping
   // SetProjection(m_inspectedCamera->GetProjection());
    //SetFOV(m_inspectedCamera->GetFieldOfView());
    //SetNearPlane(m_inspectedCamera->GetNearPlane());
    //SetFarPlane( m_inspectedCamera->GetFarPlane());

    // Make this widget visible
    this->show();
}
