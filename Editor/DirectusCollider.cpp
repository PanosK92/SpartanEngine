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

//=================================
#include "DirectusCollider.h"
#include "DirectusViewport.h"
#include "DirectusAdjustLabel.h"
#include "DirectusComboLabelText.h"
#include "DirectusDropDownButton.h"
#include "DirectusInspector.h"
#include "Components/Collider.h"
#include "Core/GameObject.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

DirectusCollider::DirectusCollider()
{

}

void DirectusCollider::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;

    m_gridLayout = new QGridLayout();
    m_gridLayout->setMargin(4);

    //= TITLE =================================================
    m_title = new QLabel("Collider");
    m_title->setStyleSheet(
                "background-image: url(:/Images/collider.png);"
                "background-repeat: no-repeat;"
                "background-position: left;"
                "padding-left: 20px;"
                );

    m_optionsButton = new DirectusDropDownButton();
    m_optionsButton->Initialize(mainWindow);
    //=========================================================

    //= TYPE ==================================================
    m_shapeTypeLabel = new QLabel("Type");
    m_shapeType = new QComboBox();
    m_shapeType->addItem("Box");
    m_shapeType->addItem("Sphere");
    m_shapeType->addItem("Static Plane");
    m_shapeType->addItem("Cylinder");
    m_shapeType->addItem("Capsule");
    m_shapeType->addItem("Cone");
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

    //= LINE ======================================
    m_line = new QWidget();
    m_line->setFixedHeight(1);
    m_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_line->setStyleSheet(QString("background-color: #585858;"));
    //=============================================

    // addWidget(widget, row, column, rowspan, colspan)
    //= GRID ==================================================
    int row = 0;

    // Row 0
    m_gridLayout->addWidget(m_title, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_optionsButton, 0, 6, 1, 1, Qt::AlignRight);
    row++;

    // Row 1 - TYPE
    m_gridLayout->addWidget(m_shapeTypeLabel,  row, 0, 1, 1);
    m_gridLayout->addWidget(m_shapeType,       row, 1, 1, 6);
    row++;

    // Row 2 - CENTER
    m_gridLayout->addWidget(m_centerLabel,                  row, 0, 1, 1);
    m_gridLayout->addWidget(m_centerX->GetLabelWidget(),    row, 1, 1, 1);
    m_gridLayout->addWidget(m_centerX->GetTextWidget(),     row, 2, 1, 1);
    m_gridLayout->addWidget(m_centerY->GetLabelWidget(),    row, 3, 1, 1);
    m_gridLayout->addWidget(m_centerY->GetTextWidget(),     row, 4, 1, 1);
    m_gridLayout->addWidget(m_centerZ->GetLabelWidget(),    row, 5, 1, 1);
    m_gridLayout->addWidget(m_centerZ->GetTextWidget(),     row, 6, 1, 1);
    row++;

    // Row 3 - SIZE
    m_gridLayout->addWidget(m_sizeLabel,                row, 0, 1, 1);
    m_gridLayout->addWidget(m_sizeX->GetLabelWidget(),  row, 1, 1, 1);
    m_gridLayout->addWidget(m_sizeX->GetTextWidget(),   row, 2, 1, 1);
    m_gridLayout->addWidget(m_sizeY->GetLabelWidget(),  row, 3, 1, 1);
    m_gridLayout->addWidget(m_sizeY->GetTextWidget(),   row, 4, 1, 1);
    m_gridLayout->addWidget(m_sizeZ->GetLabelWidget(),  row, 5, 1, 1);
    m_gridLayout->addWidget(m_sizeZ->GetTextWidget(),   row, 6, 1, 1);
    row++;

    // Row 4 - LINE
    m_gridLayout->addWidget(m_line, row, 0, 1, 7);
    //==============================================================================

    connect(m_optionsButton,    SIGNAL(Remove()),                   this, SLOT(Remove()));
    connect(m_shapeType,        SIGNAL(currentIndexChanged(int)),   this, SLOT(MapType()));
    connect(m_centerX,          SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_centerY,          SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_centerZ,          SIGNAL(ValueChanged()),             this, SLOT(MapCenter()));
    connect(m_sizeX,            SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    connect(m_sizeY,            SIGNAL(ValueChanged()),             this, SLOT(MapSize()));
    connect(m_sizeZ,            SIGNAL(ValueChanged()),             this, SLOT(MapSize()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusCollider::Reflect(weak_ptr<GameObject> gameobject)
{
    m_inspectedCollider = nullptr;

    // Catch the evil case
    if (gameobject.expired())
    {
        this->hide();
        return;
    }

    m_inspectedCollider = gameobject.lock()->GetComponent<Collider>();
    if (!m_inspectedCollider)
    {
        this->hide();
        return;
    }

    // Do the actual reflection
    ReflectType();
    ReflectCenter();
    ReflectSize();

    // Make this widget visible
    this->show();
}

void DirectusCollider::ReflectType()
{
    if (!m_inspectedCollider)
        return;

    ColliderShape shape = m_inspectedCollider->GetShapeType();
    m_shapeType->setCurrentIndex((int)shape);
}

void DirectusCollider::ReflectCenter()
{
    if (!m_inspectedCollider)
        return;

    Directus::Math::Vector3 center = m_inspectedCollider->GetCenter();
    m_centerX->SetFromFloat(center.x);
    m_centerY->SetFromFloat(center.y);
    m_centerZ->SetFromFloat(center.z);
}

void DirectusCollider::ReflectSize()
{
    if (!m_inspectedCollider)
        return;

    Directus::Math::Vector3 size = m_inspectedCollider->GetBoundingBox();
    m_sizeX->SetFromFloat(size.x);
    m_sizeY->SetFromFloat(size.y);
    m_sizeZ->SetFromFloat(size.z);
}

void DirectusCollider::MapType()
{
    int index = m_shapeType->currentIndex();

    m_inspectedCollider->SetShapeType((ColliderShape)index);
    m_inspectedCollider->UpdateShape();
}

void DirectusCollider::MapCenter()
{
    Vector3 center = Vector3(
        m_centerX->GetAsFloat(),
        m_centerY->GetAsFloat(),
        m_centerZ->GetAsFloat()
    );

    m_inspectedCollider->SetCenter(center);
    m_inspectedCollider->UpdateShape();
}

void DirectusCollider::MapSize()
{
    Vector3 size = Vector3(
        m_sizeX->GetAsFloat(),
        m_sizeY->GetAsFloat(),
        m_sizeZ->GetAsFloat()
    );

    m_inspectedCollider->SetBoundingBox(size);
    m_inspectedCollider->UpdateShape();
}

void DirectusCollider::Remove()
{
    if (!m_inspectedCollider)
        return;

    auto gameObject = m_inspectedCollider->g_gameObject;
    if (!gameObject.expired())
    {
        gameObject.lock()->RemoveComponent<Collider>();
    }

    m_inspector->Inspect(gameObject);
}
