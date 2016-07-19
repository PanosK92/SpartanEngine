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

//============================
#include "DirectusCollider.h"
#include "DirectusInspector.h"
//============================

DirectusCollider::DirectusCollider(QWidget *parent) : QWidget(parent)
{
    m_directusCore = nullptr;
    m_inspector = nullptr;
}

void DirectusCollider::Initialize(DirectusCore* directusCore, DirectusInspector* inspector)
{
    m_directusCore = directusCore;
    m_inspector = inspector;

    m_directusCore = directusCore;
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);

    //= TITLE =================================================
    m_title = new QLabel("Material");
    m_title->setStyleSheet(
                "background-image: url(:/Images/collider.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );
    //=========================================================

    //= TYPE ==================================================
    m_shapeTypeLabel = new QLabel("Type");
    m_shapeType = new QComboBox();
    m_shapeType->addItem("Box");
    m_shapeType->addItem("Capsule");
    m_shapeType->addItem("Cylinder");
    m_shapeType->addItem("Sphere");
    //=========================================================

    //= CENTER ================================================
    m_centerLabel = new QLabel("Center");
    m_centerX = new DirectusComboLabelText();
    m_centerX->Initialize("X");
    m_centerY = new DirectusComboLabelText();
    m_centerY->Initialize("Y");
    m_centerZ = new DirectusComboLabelText();
    m_centerZ->Initialize("Z");
    //=========================================================

    //= SIZE ==================================================
    m_sizeLabel = new QLabel("Size");
    m_sizeX = new DirectusComboLabelText();
    m_sizeX->Initialize("X");
    m_sizeY = new DirectusComboLabelText();
    m_sizeY->Initialize("Y");
    m_sizeZ = new DirectusComboLabelText();
    m_sizeZ->Initialize("Z");
    //=========================================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    int row = 0;

    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);
    row++;

    // Row 1 - TYPE
    m_gridLayout->addWidget(m_shapeTypeLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_shapeType,       row, 1, 1, 1);
    row++;

    // Row 2 - CENTER
    m_gridLayout->addWidget(m_centerLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_centerX,      row, 1, 1, 1);
    m_gridLayout->addWidget(m_centerY,      row, 2, 1, 1);
    m_gridLayout->addWidget(m_centerZ,      row, 3, 1, 1);
    row++;

    // Row 3 - SIZE
    m_gridLayout->addWidget(m_sizeLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_sizeX,      row, 1, 1, 1);
    m_gridLayout->addWidget(m_sizeY,      row, 2, 1, 1);
    m_gridLayout->addWidget(m_sizeZ,      row, 3, 1, 1);
    //=========================================================

    connect(m_shapeType,     SIGNAL(currentIndexChanged(int)),   this, SLOT(MapType()));
    connect(m_centerX,  SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_centerY,  SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_centerZ,  SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_sizeX,    SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    connect(m_sizeY,    SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    connect(m_sizeZ,    SIGNAL(ValueChanged()),             this, SLOT(MapSize()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusCollider::Reflect(GameObject* gameobject)
{
    m_inspectedCollider = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    m_inspectedCollider = gameobject->GetComponent<Collider>();
    if (!m_inspectedCollider)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectType(m_inspectedCollider->GetShapeType());
    ReflectCenter(m_inspectedCollider->GetCenter());
    ReflectSize(m_inspectedCollider->GetBoundingBox());

    // Make this widget visible
    this->show();
}

void DirectusCollider::ReflectType(ColliderShape shape)
{
    m_shapeType->setCurrentIndex((int)shape);
}

void DirectusCollider::ReflectCenter(Directus::Math::Vector3 center)
{
    m_centerX->SetFromFloat(center.x);
    m_centerY->SetFromFloat(center.y);
    m_centerZ->SetFromFloat(center.z);
}

void DirectusCollider::ReflectSize(Directus::Math::Vector3 size)
{
    m_sizeX->SetFromFloat(size.x);
    m_sizeY->SetFromFloat(size.y);
    m_sizeZ->SetFromFloat(size.z);
}

void DirectusCollider::MapType()
{

}

void DirectusCollider::MapCenter()
{

}

void DirectusCollider::MapSize()
{

}
