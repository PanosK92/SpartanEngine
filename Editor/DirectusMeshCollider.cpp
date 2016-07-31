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

//===============================
#include "DirectusMeshCollider.h"
#include "DirectusInspector.h"
//===============================

DirectusMeshCollider::DirectusMeshCollider(QWidget *parent) : QWidget(parent)
{

}

void DirectusMeshCollider::Initialize(DirectusCore* directusCore, DirectusInspector* inspector, QWidget* mainWindow)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Mesh Collider");
    m_title->setStyleSheet(
                "background-image: url(:/Images/meshCollider.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize();
    //=========================================================

    //= CONVEX ================================================
    m_convexLabel = new QLabel("Convex");
    m_convex = new QCheckBox();
    //=========================================================

    //= MESH ==================================================
    m_meshLabel = new QLabel("Mesh");
    m_mesh = new QLineEdit();
    m_mesh->setReadOnly(true);
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
    m_gridLayout->addWidget(m_optionsButton, 0, 1, 1, 1);

    // Row 1 - CONVEX
    m_gridLayout->addWidget(m_convexLabel, 1, 0, 1, 1);
    m_gridLayout->addWidget(m_convex, 1, 1, 1, 1);

    // Row 2 - MESH
    m_gridLayout->addWidget(m_meshLabel, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_mesh, 2, 1, 1, 1);

    // Row 3 - LINE
    m_gridLayout->addWidget(m_line, 3, 0, 1, 2);
    //============================================================

    connect(m_optionsButton, SIGNAL(Remove()), this, SLOT(Remove()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMeshCollider::Reflect(GameObject* gameobject)
{
    m_inspectedMeshCollider = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedMeshCollider = gameobject->GetComponent<MeshCollider>();
    if (!m_inspectedMeshCollider)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectConvex();
    ReflectMesh();

    // Make this widget visible
    this->show();
}

void DirectusMeshCollider::ReflectConvex()
{
    if (!m_inspectedMeshCollider)
        return;

    bool convex = m_inspectedMeshCollider->GetConvex();
    m_convex->setChecked(convex);
}

void DirectusMeshCollider::ReflectMesh()
{
    if (!m_inspectedMeshCollider)
        return;

    Mesh* mesh = m_inspectedMeshCollider->GetMesh();
    if (!mesh)
        return;

    m_mesh->setText(QString::fromStdString(mesh->name));
}

void DirectusMeshCollider::MapConvex()
{
    if (!m_inspectedMeshCollider || !m_directusCore)
        return;

    bool convex = m_convex->isChecked();
    m_inspectedMeshCollider->SetConvex(convex);

    m_directusCore->Update();
}

void DirectusMeshCollider::MapMesh()
{

}

void DirectusMeshCollider::Remove()
{
    if (!m_inspectedMeshCollider)
        return;

    GameObject* gameObject = m_inspectedMeshCollider->g_gameObject;
    gameObject->RemoveComponent<MeshCollider>();
    m_directusCore->Update();

    m_inspector->Inspect(gameObject);
}
