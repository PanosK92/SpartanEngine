#include "Directus3DWidget.h"
#include "Core/Engine.h"
#include "Core/Socket.h"

// CONSTRUCTOR/DECONSTRUCTOR =========================
Directus3DWidget::Directus3DWidget(QWidget *parent)
 : QWidget(parent) {

    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);

    InitializeEngine();
    Resize(this->size().width(), this->size().height());
}

Directus3DWidget::~Directus3DWidget()
{
    ShutdownEngine();
}

//= OVERRIDDEN FUNCTIONS ============================
void Directus3DWidget::resizeEvent(QResizeEvent* evt)
{
    int width = evt->size().width();
    int height = evt->size().width();

    Resize(width, height);
}

void Directus3DWidget::paintEvent(QPaintEvent* evt)
{
    Render();
}
//===================================================

//= Engine functions ================================
void Directus3DWidget::InitializeEngine()
{
    // Create and initialize Directus3D
    m_engine = new Engine();
    HINSTANCE hInstance = (HINSTANCE)::GetModuleHandle(NULL);
    HWND mainWindowHandle = (HWND)this->parentWidget()->winId();
    HWND widgetHandle = (HWND)this->winId();
    m_engine->Initialize(hInstance, mainWindowHandle, widgetHandle);

    // Get the socket
    m_socket = m_engine->GetSocket();
}

void Directus3DWidget::ShutdownEngine()
{
    m_engine->Shutdown();
    delete m_engine;
}

void Directus3DWidget::Render()
{
    if (m_socket)
        m_socket->Run();
}

void Directus3DWidget::Resize(int width, int height)
{
    if (m_socket)
        m_socket->SetViewport(width, height);
}
//==================================================
