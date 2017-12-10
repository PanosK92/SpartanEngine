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

//= INCLUDES ==================
#include "DirectusMeshFilter.h"
#include "DirectusInspector.h"
//=============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusMeshFilter::DirectusMeshFilter()
{

}

void DirectusMeshFilter::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_title = new QLabel("Mesh Filter");
    m_title->setStyleSheet(
                "background-image: url(:/Images/meshFilter.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
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
    m_gridLayout->addWidget(m_title,            0, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton,    0, 1, 1, 1, Qt::AlignRight);

    // Row 1 - MESH
    m_gridLayout->addWidget(m_meshLabel,        1, 0, 1, 1);
    m_gridLayout->addWidget(m_mesh,             1, 1, 1, 1);

    // Row 2 - LINE
    m_gridLayout->addWidget(m_line,             2, 0, 1, 2);
    //============================================================

    //= SET GRID SPACING ===================================
    m_gridLayout->setHorizontalSpacing(m_horizontalSpacing);
    m_gridLayout->setVerticalSpacing(m_verticalSpacing);
    //======================================================

    connect(m_optionsButton,        SIGNAL(Remove()),                   this, SLOT(Remove()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusMeshFilter::Reflect(weak_ptr<GameObject> gameobject)
{
    m_inspectedMeshFilter = nullptr;

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedMeshFilter = gameobject.lock()->GetComponent<MeshFilter>()._Get();
    if (!m_inspectedMeshFilter)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectMesh();

    // Make this widget visible
    this->show();
}

void DirectusMeshFilter::ReflectMesh()
{
    if (!m_inspectedMeshFilter)
        return;

    QString meshName = QString::fromStdString(m_inspectedMeshFilter->GetMeshName());
    m_mesh->setText(meshName);
}

void DirectusMeshFilter::MapMesh()
{

}

void DirectusMeshFilter::Remove()
{
    if (!m_inspectedMeshFilter)
        return;

    auto gameObject = m_inspectedMeshFilter->g_gameObject;
    if (!gameObject.expired())
    {
        gameObject.lock()->RemoveComponent<MeshFilter>();
    }

    m_inspector->Inspect(gameObject);
}
