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
#include "Logging/Log.h"
#include "Graphics/Texture.h"
//=============================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

#define SLOT_SIZE 40

DirectusMaterialTextureDropTarget::DirectusMaterialTextureDropTarget(QWidget *parent) : QLabel(parent)
{
    setAcceptDrops(true);
    m_inspector = nullptr;
    m_texture = nullptr;
}

void DirectusMaterialTextureDropTarget::Initialize(DirectusInspector* inspector, TextureType textureType)
{
    m_inspector = inspector;
    m_textureType = textureType;

    // Timer which checks if the thumbnail image is loaded (async)
    m_timer500ms = new QTimer(this);
    connect(m_timer500ms, SIGNAL(timeout()), this, SLOT(Update()));
    m_timer500ms->start(500);

    this->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    this->setMinimumSize(SLOT_SIZE, SLOT_SIZE);
    this->setStyleSheet(
                "background-color: #484848;"
                "border-color: #212121;"
                "border-style: inset;"
                );
}

void DirectusMaterialTextureDropTarget::LoadImageAsync(const std::string& filePath)
{
    if (m_currentFilePath == filePath || filePath == NOT_ASSIGNED || filePath.empty())
        return;

    m_currentFilePath = filePath;
    m_texture = new Texture(m_inspector->GetContext());
    m_texture->SetUsage(TextureUsage_External);
    m_texture->LoadFromFile(filePath);
}

void DirectusMaterialTextureDropTarget::Update()
{
    if (!m_texture)
        return;

    if (m_texture->GetAsyncState() == Async_Failed)
    {
        delete m_texture;
        m_texture = nullptr;
        return;
    }

    if (m_texture->GetAsyncState() != Async_Completed)
        return;

    QImage image = QImage((const uchar*)m_texture->GetRGBA().front().data(), m_texture->GetWidth(), m_texture->GetHeight(), QImage::Format_RGBA8888);
    QPixmap pixmap = QPixmap::fromImage(image);
    if (m_texture->GetWidth() != SLOT_SIZE || m_texture->GetHeight() != SLOT_SIZE)
    {
        pixmap = pixmap.scaled(SLOT_SIZE, SLOT_SIZE, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    this->setPixmap(pixmap);

    delete m_texture;
    m_texture = nullptr;
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

    if (FileSystem::IsSupportedImageFile(imagePath) ||FileSystem::IsEngineTextureFile(imagePath))
    {
        // Get the currently inspected material
        auto material = m_inspector->GetMaterialComponent()->GetInspectedMaterial().lock();
        if (!material)
            return;

        // Set the texture to the material
        auto texture = m_inspector->GetContext()->GetSubsystem<ResourceManager>()->Load<Texture>(imagePath);

        if (!texture.expired())
        {
            texture.lock()->SetType(m_textureType);
            material->SetTexture(texture);
            material->SaveToFile(material->GetResourceFilePath());
            LoadImageAsync(imagePath);
        }
    }
}
//=========================================================================================
