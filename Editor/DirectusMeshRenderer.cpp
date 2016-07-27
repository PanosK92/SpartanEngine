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
#include "IO/Log.h"
//===============================

DirectusMeshRenderer::DirectusMeshRenderer(QWidget *parent) : QWidget(parent)
{

}

void DirectusMeshRenderer::Initialize()
{
    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Mesh Renderer");
    m_title->setStyleSheet(
                "background-image: url(:/Images/meshRenderer.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //=========================================================

    //= CAST SHADOWS ==========================================
    m_castShadowsLabel = new QLabel("Cast Shadows");
    m_castShadowsCheckBox = new QCheckBox();
    //=========================================================

    //= RECEIVE SHADOWS =======================================
    m_receiveShadowsLabel = new QLabel("Receive Shadows");
    m_receiveShadowsCheckBox = new QCheckBox();
    //=========================================================

    //= MATERIAL ==============================================
    m_materialLabel = new QLabel("Material");
    m_material = new QLineEdit();
    m_material->setReadOnly(true);
    //=========================================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 2);

    // Row 1
    m_gridLayout->addWidget(m_castShadowsLabel, 1, 0, 1, 1);
    m_gridLayout->addWidget(m_castShadowsCheckBox, 1, 1, 1, 1);

    // Row 2
    m_gridLayout->addWidget(m_receiveShadowsLabel, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_receiveShadowsCheckBox, 2, 1, 1, 1);

    // Row 3
    m_gridLayout->addWidget(m_materialLabel, 3, 0, 1, 1);
    m_gridLayout->addWidget(m_material, 3, 1, 1, 1);

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line, 4, 0, 1, 2);
    //============================================================


    connect(m_castShadowsCheckBox,      SIGNAL(clicked(bool)), this, SLOT(Map()));
    connect(m_receiveShadowsCheckBox,   SIGNAL(clicked(bool)), this, SLOT(Map()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMeshRenderer::Reflect(GameObject* gameobject)
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

    // Do the actual reflection
    SetCastShadows(m_inspectedMeshRenderer->GetCastShadows());
    SetReceiveShadows(m_inspectedMeshRenderer->GetReceiveShadows());
    SetMaterial(m_inspectedMeshRenderer->GetMaterial());

    // Make this widget visible
    this->show();
}

void DirectusMeshRenderer::SetCastShadows(bool cast)
{
     m_castShadowsCheckBox->setChecked(cast);
}

void DirectusMeshRenderer::SetReceiveShadows(bool receive)
{
    m_receiveShadowsCheckBox->setChecked(receive);
}

void DirectusMeshRenderer::SetMaterial(Material* material)
{
    if (!material)
        return;

    std::string materialName = material->GetName();
    m_material->setText(QString::fromStdString(materialName));
}

void DirectusMeshRenderer::Map()
{
    if (!m_inspectedMeshRenderer)
        return;

    bool castShadows = m_castShadowsCheckBox->isChecked();  
    m_inspectedMeshRenderer->SetCastShadows(castShadows);

    bool receiveShadows = m_receiveShadowsCheckBox->isChecked();
    m_inspectedMeshRenderer->SetReceiveShadows(receiveShadows);
}
