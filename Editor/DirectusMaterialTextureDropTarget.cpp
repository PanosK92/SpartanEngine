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

//============================================
#include "DirectusMaterialTextureDropTarget.h"
#include "DirectusAssetLoader.h"
#include "DirectusInspector.h"
#include "DirectusMaterial.h"
#include "DirectusViewport.h"
#include <QThread>
#include <QDragMoveEvent>
#include <QMimeData>
#include "Logging/Log.h"
#include "Resource/ResourceManager.h"
#include "FileSystem/FileSystem.h"
#include "Components/MeshRenderer.h"
#include "Graphics/Texture.h"
#include "Graphics/Material.h"
//=============================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusMaterialTextureDropTarget::DirectusMaterialTextureDropTarget(QWidget *parent) : QLabel(parent)
{
    setAcceptDrops(true);
}

void DirectusMaterialTextureDropTarget::Initialize(DirectusInspector* inspector, TextureType textureType)
{
    m_inspector = inspector;
    m_textureType = textureType;

    /*
    widget background dark:         292929
    widget background light:		383838
    widget background highlighted: 	484848
    text color:                     909090
    text highlighted:               EDEDED
    border:                         212121
    border highlighted:             464646
    text edit background:           414141
    */

    this->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    this->setMinimumSize(20,20);
    this->setStyleSheet(
                "background-color: #484848;"
                "border-color: #212121;"
                "border-style: inset;"
                );
}

void DirectusMaterialTextureDropTarget::LoadImageAsync(std::string filePath)
{
    if (m_currentFilePath == filePath)
        return;

    m_currentFilePath = filePath;
    QThread* thread = new QThread();
    DirectusAssetLoader* imageLoader = new DirectusAssetLoader();

    imageLoader->moveToThread(thread);
    imageLoader->PrepareForTexture(filePath, 20, 20);

    connect(thread,         SIGNAL(started()),              imageLoader,    SLOT(LoadTexture()));
    connect(imageLoader,    SIGNAL(ImageReady(QPixmap)),    this,           SLOT(setPixmap(QPixmap)));
    connect(imageLoader,    SIGNAL(Finished()),             thread,         SLOT(quit()));
    connect(imageLoader,    SIGNAL(Finished()),             imageLoader,    SLOT(deleteLater()));
    connect(thread,         SIGNAL(finished()),             thread,         SLOT(deleteLater()));

    thread->start(QThread::HighestPriority);
}

//= DROP ============================================================================
void DirectusMaterialTextureDropTarget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusMaterialTextureDropTarget::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusMaterialTextureDropTarget::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Get the path of the texture being dragged
    QString absolutePath = event->mimeData()->text();
    std::string imagePath = FileSystem::GetRelativeFilePath(absolutePath.toStdString());

    if (FileSystem::IsSupportedImageFile(imagePath))
    {
        // Get the currently inspected material
        auto material = m_inspector->GetMaterialComponent()->GetInspectedMaterial().lock();
        if (!material)
            return;

        // Set the texture to the material
        auto context = m_inspector->GetSocket()->GetContext();
        auto texture = context->GetSubsystem<ResourceManager>()->Load<Texture>(imagePath);

        if (!texture.expired())
        {
            texture._Get()->SetTextureType(m_textureType);
            material->SetTexture(texture);
            material->SaveToExistingDirectory();
            LoadImageAsync(imagePath);
        }
    }
}
//=========================================================================================
