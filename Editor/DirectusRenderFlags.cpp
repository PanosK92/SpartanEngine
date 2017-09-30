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

//= INCLUDES ===================
#include "DirectusRenderFlags.h"
#include "Logging/Log.h"
#include <QLineEdit>
#include <QListView>
#include "Graphics/Renderer.h"
//==============================

//= NAMESPACES ==========
using namespace Directus;
//=======================

DirectusRenderFlags::DirectusRenderFlags(QWidget* parent) : QComboBox(parent)
{
    m_context = nullptr;
    m_renderer = nullptr;

    m_rows = 8;
    m_model = new QStandardItemModel(m_rows, 1);
    this->setModel(m_model);

    AddCheckItem(0, "Albedo");
    AddCheckItem(1, "Normal");
    AddCheckItem(2, "Depth");
    AddCheckItem(3, "Material");
    AddCheckItem(4, "Physics");
    AddCheckItem(5, "Bounding Boxes");
    AddCheckItem(6, "Mouse Picking Ray");
    AddCheckItem(7, "Grid");
    AddCheckItem(8, "Performance Metrics");

    // Called when the user clicks an item
    connect((QListView*) view(), SIGNAL(pressed(QModelIndex)), this, SLOT(OnItemPressed(QModelIndex)));
    // Called when the user clicks the checkbox of an item
    connect(m_model, SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)), this, SLOT(OnCheckBoxPressed()));

    this->setEditable(true);
    m_defaultText = "Debug Flags";
    //this->lineEdit()->setText(m_defaultText);
    //this->lineEdit()->setReadOnly(true);

    this->setItemDelegate(new QCheckListStyledItemDelegate(this));
}

DirectusRenderFlags::~DirectusRenderFlags()
{
    delete m_model;
}

void DirectusRenderFlags::Initialize(Context* context)
{
    if (!context)
        return;

    m_context = context;
    m_renderer = m_context->GetSubsystem<Renderer>();
}

void DirectusRenderFlags::showPopup()
{
    ReflectRenderFlags();
    QComboBox::showPopup();
}

void DirectusRenderFlags::OnItemPressed(const QModelIndex &index)
{
    if (!m_model)
        return;

    QStandardItem* item = m_model->itemFromIndex(index);
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    MapRenderFlags();
}

void DirectusRenderFlags::OnCheckBoxPressed()
{
    MapRenderFlags();
}

void DirectusRenderFlags::AddCheckItem(int row, const QString& text)
{
    if (!m_model)
        return;

    QStandardItem* item = new QStandardItem(text);
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    item->setData(Qt::Unchecked, Qt::CheckStateRole);

    m_model->setItem(row, 0, item);
}

void DirectusRenderFlags::MapRenderFlags()
{
    if (!m_renderer || !m_model)
        return;

    unsigned long flags = 0;
    for (int i = 0; i <= m_rows; i++)
    {
        auto checkState = m_model->item(i)->checkState();
        if (checkState != Qt::Checked)
            continue;

        if (i == 0)
        {
            flags |= Render_Albedo;
        }
        else if (i == 1)
        {
            flags |= Render_Normal;
        }
        else if (i == 2)
        {
            flags |= Render_Depth;
        }
        else if (i == 3)
        {
            flags |= Render_Material;
        }
        else if (i == 4)
        {
            flags |= Render_Physics;
        }
        else if (i == 5)
        {
            flags |= Render_Bounding_Boxes;
        }
        else if (i == 6)
        {
            flags |= Render_Mouse_Picking_Ray;
        }
        else if (i == 7)
        {
            flags |= Render_Grid;
        }
        else if (i == 8)
        {
            flags |= Render_Performance_Metrics;
        }
    }

    m_renderer->SetRenderFlags(flags);
}

void DirectusRenderFlags::ReflectRenderFlags()
{
    if (!m_renderer || !m_model)
        return;

    auto flags = m_renderer->GetRenderFlags();

    for (int i = 0; i <= m_rows; i++)
    {
        if (i == 0)
        {
            m_model->item(i)->setCheckState((flags & Render_Albedo) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 1)
        {
            m_model->item(i)->setCheckState((flags & Render_Normal) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 2)
        {
            m_model->item(i)->setCheckState((flags & Render_Depth) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 3)
        {
            m_model->item(i)->setCheckState((flags & Render_Material) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 4)
        {
            m_model->item(i)->setCheckState((flags & Render_Physics) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 5)
        {
            m_model->item(i)->setCheckState((flags & Render_Bounding_Boxes) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 6)
        {
            m_model->item(i)->setCheckState((flags & Render_Mouse_Picking_Ray) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 7)
        {
            m_model->item(i)->setCheckState((flags & Render_Grid) ? Qt::Checked : Qt::Unchecked);
        }
        else if (i == 8)
        {
            m_model->item(i)->setCheckState((flags & Render_Performance_Metrics) ? Qt::Checked : Qt::Unchecked);
        }
    }
}
