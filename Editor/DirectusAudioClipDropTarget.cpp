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

//= INCLUDES ===========================
#include "DirectusAudioClipDropTarget.h"
#include "FileSystem/FileSystem.h"
#include "DirectusInspector.h"
#include "Components/AudioSource.h"
#include <QDragMoveEvent>
#include <QMimeData>
#include "Logging/Log.h"
//======================================

//= INCLUDES ============
using namespace std;
using namespace Directus;
//=======================

DirectusAudioClipDropTarget::DirectusAudioClipDropTarget(QWidget* parent)
{
    setAcceptDrops(true);
    setReadOnly(true);
}

void DirectusAudioClipDropTarget::Initialize(DirectusInspector* inspector)
{
     m_inspector = inspector;
}

void DirectusAudioClipDropTarget::dragEnterEvent(QDragEnterEvent *event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsSupportedAudioFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusAudioClipDropTarget::dragMoveEvent(QDragMoveEvent *event)
{
    // All data is passed around via text, so check if it's there
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsSupportedAudioFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusAudioClipDropTarget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (!FileSystem::IsSupportedAudioFile(event->mimeData()->text().toStdString()))
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Get the path of the material being dragged
    string audioClipPath = event->mimeData()->text().toStdString();

    // Set the audio clip to the audio source component
    string clipName =" N/A";
    auto inspectedGameObj = m_inspector->GetInspectedGameObject();
    if (!inspectedGameObj.expired())
    {
        AudioSource* audioSource = inspectedGameObj.lock()->GetComponent<AudioSource>();
        audioSource->LoadAudioClip(audioClipPath);
        clipName = audioSource->GetAudioClipName();
    }

    // Set the text of the QLineEdit
    this->setText(QString::fromStdString(clipName));
}
