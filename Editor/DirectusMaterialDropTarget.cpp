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

// INCLUDES ============================
#include "DirectusMaterialDropTarget.h"
#include "DirectusCore.h"
#include "DirectusInspector.h"
#include <QDragMoveEvent>
#include <QMimeData>
#include "Components/MeshRenderer.h"
#include "DirectusMaterial.h"
//======================================

//= NAMESPACES ==========
using namespace Directus;
using namespace std;
//=======================

DirectusMaterialDropTarget::DirectusMaterialDropTarget(QWidget* parent) : QLineEdit(parent)
{
    setAcceptDrops(true);
    setReadOnly(true);
}

void DirectusMaterialDropTarget::Initialize(DirectusInspector* inspector)
{
    m_inspector = inspector;
}

//= DROP ============================================================================
void DirectusMaterialDropTarget::dragEnterEvent(QDragEnterEvent* event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsEngineMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusMaterialDropTarget::dragMoveEvent(QDragMoveEvent* event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsEngineMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusMaterialDropTarget::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsEngineMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Get the path of the material being dragged
    std::string materialPath = event->mimeData()->text().toStdString();

    // Get currently inspected GameObject
    auto gameObject = m_inspector->GetInspectedGameObject();
    if (gameObject.expired())
        return;

    // Load the material
    std::weak_ptr<Material> material = gameObject.lock()->GetComponent<MeshRenderer>()->LoadMaterial(materialPath);

    // Set the material editor component to reflect thew newly loaded material
    m_inspector->GetMaterialComponent()->Reflect(gameObject);

    // Set the text of the QLineEdit
    this->setText(QString::fromStdString(!material.expired() ? material.lock()->GetResourceName() : DATA_NOT_ASSIGNED));

    // Emit a signal with the material
    emit MaterialDropped(material);
}
//=========================================================================================
