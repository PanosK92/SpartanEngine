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

//= INCLUDES =================
#include "DirectusViewport.h"
#include "Logging/Log.h"
#include <QStyleOption>
#include "Core/Settings.h"
#include "Core/GameObject.h"
#include "Core/Context.h"
#include "Core/Scene.h"
#include "Components/Camera.h"
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "DirectusInspector.h"
#include "Graphics/Renderer.h"
//============================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

// CONSTRUCTOR/DECONSTRUCTOR =========================
DirectusViewport::DirectusViewport(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_MSWindowsUseDirect3D, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);

    // This will make Qt update this widget as fast as possible.
    // Yes, paintEvent(QPaintEvent*) will be called also.
    // NOTE: I tested this technique and it yields thousands
    // of FPS, so it should do.
    m_timerUpdate = new QTimer(this);
    connect(m_timerUpdate, SIGNAL(timeout()), this, SLOT(update()));

    // light update
    m_timer60FPS = new QTimer(this);
    connect(m_timer60FPS, SIGNAL(timeout()), this, SLOT(Update60FPS()));
    m_timer60FPS->start(16);

    m_locked = false;
    m_isRunning = false;
}

DirectusViewport::~DirectusViewport()
{
    m_engine->Shutdown();
    delete m_engine;
}

void DirectusViewport::Initialize(void* mainWindowHandle, void* hInstance)
{
    // Initialize the engine
    m_engine = new Engine(new Context());
    m_engine->SetHandles(hInstance, mainWindowHandle, (void*)this->winId());
    m_engine->Initialize();

    m_context = m_engine->GetContext();
    m_renderer = m_context->GetSubsystem<Renderer>();
}

bool DirectusViewport::IsRunning()
{
    return m_isRunning;
}

// Runs when the play button is pressed
void DirectusViewport::Start()
{
    if (m_locked)
        return;

    m_engine->GetContext()->GetSubsystem<Scene>()->Start();
    m_timerUpdate->start(0);
    m_timer60FPS->stop();
    m_isRunning = true;

    emit EngineStarting();
}

// Runs when the play button is released
void DirectusViewport::Stop()
{
    if (m_locked)
        return;

    m_engine->GetContext()->GetSubsystem<Scene>()->OnDisable();
    m_timerUpdate->stop();
    m_timer60FPS->start(16);
    m_isRunning = false;

    emit EngineStopping();
}

// Ticks the engine as fast as possible, in Game Mode
void DirectusViewport::Update()
{
    if (m_locked)
        return;

    m_engine->SetMode(Game);
    m_engine->Update();
}

// Ticks the engine at 60Hz, in Editor mode
void DirectusViewport::Update60FPS()
{
    if (m_locked)
        return;

    m_engine->SetMode(Editor);
    m_engine->Update();
}

// Prevents any engine update to execute
void DirectusViewport::LockUpdate()
{
    m_locked = true;
}

// Allows any engine update function to execute
void DirectusViewport::UnlockUpdate()
{
    m_locked = false;
}
//====================================================

//= OVERRIDDEN FUNCTIONS =============================
void DirectusViewport::resizeEvent(QResizeEvent* evt)
{
    if (evt->oldSize() == evt->size())
        return;

    int width = this->size().width();
    int height = this->size().height();

    if (width % 2 != 0)
        width++;

    if (height % 2 != 0)
        height++;

    // Change the size of the widget
    this->setGeometry(QRect(0, 0, width, height));

    // Change the resolution of the engine
    SetResolution(width, height);
}

// Invoked by QT itself, Update() let's the engine do the rendering
void DirectusViewport::paintEvent(QPaintEvent* evt)
{
    Update();
}

void DirectusViewport::mousePressEvent(QMouseEvent* event)
{
    QPoint mousePos = event->pos();
    weak_ptr<GameObject> camera = m_context->GetSubsystem<Scene>()->GetMainCamera();
    weak_ptr<GameObject> picked = camera._Get()->GetComponent<Camera>()._Get()->Pick(Vector2(mousePos.x(), mousePos.y()));

    if (!picked.expired())
    {
        emit GameObjectPicked(picked._Get());
    }
}
//===================================================

//= Engine functions ================================
// Changes the rendering resolution of the engine
void DirectusViewport::SetResolution(float width, float height)
{
    if (!m_context)
        return;

    m_context->GetSubsystem<Renderer>()->SetResolution(width, height);
    m_context->GetSubsystem<Renderer>()->SetViewport(width, height);
}
//===================================================
