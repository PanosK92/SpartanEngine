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
#include "DirectusCore.h"
#include "Logging/Log.h"
#include <QStyleOption>
#include "Core/Settings.h"
#include "Core/Context.h"
#include "Core/Scene.h"
#include "Components/Camera.h"
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "DirectusInspector.h"
//============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

// CONSTRUCTOR/DECONSTRUCTOR =========================
DirectusCore::DirectusCore(QWidget* parent) : QWidget(parent)
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

    m_timerPerSec = new QTimer(this);
    connect(m_timerPerSec, SIGNAL(timeout()), this, SLOT(UpdatePerSec()));

    m_locked = false;
    m_isRunning = false;
}

DirectusCore::~DirectusCore()
{
    ShutdownEngine();
}

Socket* DirectusCore::GetEngineSocket()
{
    return m_socket;
}

void DirectusCore::Initialize(HWND mainWindowHandle, HINSTANCE hInstance, DirectusStatsLabel* directusStatsLabel)
{
    // Initialize the engine
    m_engine = new Engine(new Context());
    m_engine->Initialize(hInstance, mainWindowHandle, (HWND)this->winId());

    m_socket = m_engine->GetContext()->GetSubsystem<Socket>();
    m_directusStatsLabel = directusStatsLabel;
}

void DirectusCore::SetInspector(DirectusInspector* inspector)
{
    m_inspector = inspector;
}

bool DirectusCore::IsRunning()
{
    return m_isRunning;
}

void DirectusCore::Start()
{
    if (m_locked)
        return;

    m_timerUpdate->start(0);
    m_timerPerSec->start(1000);
    m_isRunning = true;

    emit EngineStarting();
}

void DirectusCore::Stop()
{
    if (m_locked)
        return;

    m_timerUpdate->stop();
    m_timerPerSec->stop();
    m_isRunning = false;

    emit EngineStopping();
}

// Runs as fast as possible, performs a full simulation cycle.
void DirectusCore::Update()
{
    if (m_locked)
        return;

    m_socket->Update();
}

// Updates engine's subsystems and propagates data, it doesn't simulate
void DirectusCore::LightUpdate()
{
    m_socket->LightUpdate();
}

// A function that runs every second
void DirectusCore::UpdatePerSec()
{
    if (m_locked)
        return;

    m_directusStatsLabel->UpdateStats(this);
}

// Locks the Update() function
void DirectusCore::LockUpdate()
{
    m_locked = true;
}

// Unlocks the Update() function
void DirectusCore::UnlockUpdate()
{
    m_locked = false;
}
//====================================================

//= OVERRIDDEN FUNCTIONS =============================
void DirectusCore::resizeEvent(QResizeEvent* evt)
{
    if (evt->oldSize() == evt->size())
        return;

    int width = this->size().width();
    int height = this->size().height();

    //float aspectRatio = m_socket->
    height = width / (16.0f/9.0f);

    if (width % 2 != 0)
        width++;

    if (height % 2 != 0)
        height++;

    // Change the size of the widget
    setGeometry(QRect(0, 0, width, height));

    // Change the rendering resolution of the engine
    SetResolution(width, height);
}

// Invoked by QT itself, Update() let's the engine do the rendering
void DirectusCore::paintEvent(QPaintEvent* evt)
{
    Update();
}

// Temporary
void DirectusCore::mousePressEvent(QMouseEvent* event)
{
    QPoint mousePos = event->pos();
    auto picked = m_socket->GetContext()->GetSubsystem<Scene>()->MousePick(Vector2(mousePos.x(), mousePos.y()));

    if (picked)
        LOG_INFO(picked->GetName());

    m_inspector->Inspect(picked);
}
//===================================================

//= Engine functions ================================
// Shuts down the engine
void DirectusCore::ShutdownEngine()
{
	m_engine->Shutdown();
	delete m_engine;
}

// Changes the rendering resolution of the engine
void DirectusCore::SetResolution(int width, int height)
{
    if (!m_socket)
        return;

	m_socket->SetViewport(width, height);
}
//===================================================
