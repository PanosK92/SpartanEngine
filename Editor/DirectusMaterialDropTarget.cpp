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
#include "DirectusQLineEditDropTarget.h"
#include "DirectusCore.h"
#include "DirectusInspector.h"
#include <QDragMoveEvent>
#include <QMimeData>
#include "Components/MeshRenderer.h"
//======================================

DirectusQLineEditDropTarget::DirectusQLineEditDropTarget(QWidget* parent) : QLineEdit(parent)
{
    setAcceptDrops(true);
    setReadOnly(true);
    //setEnabled(false);
}

void DirectusQLineEditDropTarget::Initialize(DirectusCore* directusCore, DirectusInspector* inspector)
{
    m_directusCore = directusCore;
    m_inspector = inspector;
}

//= DROP ============================================================================
void DirectusQLineEditDropTarget::dragEnterEvent(QDragEnterEvent* event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusQLineEditDropTarget::dragMoveEvent(QDragMoveEvent* event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusQLineEditDropTarget::dropEvent(QDropEvent* event)
{
    GameObject* gameObject = m_inspector->GetInspectedGameObject();

    if (!gameObject || !event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsMaterialFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Get the path of the material being dragged
    const QMimeData* mime = event->mimeData();
    std::string materialPath = mime->text().toStdString();

    // This is essential to avoid an absolute path mess. Everything is relative.
    materialPath = FileSystem::GetRelativePathFromAbsolutePath(materialPath);

    // Set the material to the mesh renderer
    gameObject->GetComponent<MeshRenderer>()->SetMaterial(materialPath);

    // Update the engine
    m_directusCore->Update();
}
//=========================================================================================
