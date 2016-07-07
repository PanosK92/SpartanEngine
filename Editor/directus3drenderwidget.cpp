#include "directus3drenderwidget.h"
#include "Core/Engine.h"

void Create()
{

}

void Render()
{

}

void Resize()
{

}

Directus3DRenderWidget::Directus3DRenderWidget(QObject *parent) : QObject(parent)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);

    Create();
}

void Directus3DRenderWidget::paintEvent(QPaintEvent* evt)
{
    Render();
}
