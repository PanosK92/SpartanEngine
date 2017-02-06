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
#include "Logging/Log.h"
#include "DirectusInspector.h"
//===============================

DirectusMeshRenderer::DirectusMeshRenderer()
{

}

void DirectusMeshRenderer::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;

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

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
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
    m_material = new DirectusMaterialDropTarget();
    m_material->Initialize(m_inspector);
    //=========================================================

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    // Row 0 - TITLE
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton, 0, 1, 1, 1, Qt::AlignRight);

    // Row 1 - CAST SHADOWS
    m_gridLayout->addWidget(m_castShadowsLabel, 1, 0, 1, 1);
    m_gridLayout->addWidget(m_castShadowsCheckBox, 1, 1, 1, 1);

    // Row 2 - RECEIVE SHADOWS
    m_gridLayout->addWidget(m_receiveShadowsLabel, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_receiveShadowsCheckBox, 2, 1, 1, 1);

    // Row 3 - MATERIAL
    m_gridLayout->addWidget(m_materialLabel, 3, 0, 1, 1);
    m_gridLayout->addWidget(m_material, 3, 1, 1, 1);

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line, 4, 0, 1, 3);
    //============================================================

    // Gear button on the top left
    connect(m_optionsButton,            SIGNAL(Remove()),       this, SLOT(Remove()));
    // Cast shadows check box
    connect(m_castShadowsCheckBox,      SIGNAL(clicked(bool)),  this, SLOT(MapCastShadows()));
    // Receive shadows check box
    connect(m_receiveShadowsCheckBox,   SIGNAL(clicked(bool)),  this, SLOT(MapReceiveShadows()));
    // DirectusQLineEditDropTarget
    connect(m_material,   SIGNAL(MaterialDropped(std::weak_ptr<Material>)),  this, SLOT(DoMaterialInspCompReflection()));

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
    ReflectCastShadows();
    ReflectReceiveShadows();
    ReflectMaterial();

    // Make this widget visible
    this->show();
}

void DirectusMeshRenderer::ReflectCastShadows()
{
    if (!m_inspectedMeshRenderer)
        return;

    bool cast = m_inspectedMeshRenderer->GetCastShadows();
    m_castShadowsCheckBox->setChecked(cast);
}

void DirectusMeshRenderer::ReflectReceiveShadows()
{
    if (!m_inspectedMeshRenderer)
        return;

    bool receive = m_inspectedMeshRenderer->GetReceiveShadows();
    m_receiveShadowsCheckBox->setChecked(receive);
}

void DirectusMeshRenderer::ReflectMaterial()
{
    auto material = m_inspectedMeshRenderer->GetMaterial();
    if (material.expired())
        return;

    std::string materialName = material.lock()->GetResourceName();
    m_material->setText(QString::fromStdString(materialName));
}

// This makes the material component of the inspector to reflect
// a material. It has little to do with DirectusMaterialDropTarget.
void DirectusMeshRenderer::DoMaterialInspCompReflection()
{
    auto material = m_inspectedMeshRenderer->GetMaterial();

    if (!m_materialUIComp || material.expired())
        return;

    m_materialUIComp->Reflect(m_inspectedMeshRenderer->g_gameObject);
}

void DirectusMeshRenderer::MapCastShadows()
{
    if (!m_inspectedMeshRenderer)
        return;

    bool castShadows = m_castShadowsCheckBox->isChecked();
    m_inspectedMeshRenderer->SetCastShadows(castShadows);
}

void DirectusMeshRenderer::MapReceiveShadows()
{
    if (!m_inspectedMeshRenderer)
        return;

    bool receiveShadows = m_receiveShadowsCheckBox->isChecked();
    m_inspectedMeshRenderer->SetReceiveShadows(receiveShadows);
}

void DirectusMeshRenderer::Remove()
{
    if (!m_inspectedMeshRenderer)
        return;

    GameObject* gameObject = m_inspectedMeshRenderer->g_gameObject;
    gameObject->RemoveComponent<MeshRenderer>();

    m_inspector->Inspect(gameObject);
}
