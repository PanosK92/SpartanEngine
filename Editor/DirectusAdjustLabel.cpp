// INCLUDES ====================
#include "DirectusAdjustLabel.h"
#include <QMouseEvent>
#include "IO/Log.h"
#include <QApplication>
#include <QDesktopWidget>
//==============================

DirectusAdjustLabel::DirectusAdjustLabel(QWidget *parent) : QLabel(parent)
{
    // Required by mouseMoveEvent(QMouseEvent*)
    this->setMouseTracking(true);

    m_isMouseHovering = false;
    m_isMouseDragged = false;
}

void DirectusAdjustLabel::AdjustQLineEdit(QLineEdit* lineEdit)
{
    m_lineEdit = lineEdit;
}

// The mouse cursor is hovering above the label
void DirectusAdjustLabel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isMouseHovering)
       MouseEntered();

    m_isMouseHovering = true;

    // Change cursor to <->
    this->setCursor(Qt::SizeHorCursor);

    // Forward the original event
    QLabel::mouseMoveEvent(event);

     if(event->buttons() == Qt::LeftButton)
        m_isMouseDragged = true;
     else
        m_isMouseDragged = false;

     Adjust();
}

// The mouse cursor just left the widget
void DirectusAdjustLabel::leaveEvent(QEvent* event)
{
    m_isMouseHovering = false;

    // Change cursor to the classic arrow
    this->setCursor(Qt::ArrowCursor);

    // Forward the original event
    QLabel::leaveEvent(event);
}

void DirectusAdjustLabel::MouseEntered()
{
    m_currentTexBoxValue = GetTextBoxValue();
}

QPoint DirectusAdjustLabel::GetMousePosLocal()
{
    QPoint mousePos = this->mapFromGlobal(QCursor::pos());
    return mousePos;
}

float DirectusAdjustLabel::GetTextBoxValue()
{
    if (!m_lineEdit)
        return 0;

    return m_lineEdit->text().toFloat();
}

void DirectusAdjustLabel::SetTextBoxValue(float value)
{
    if (!m_lineEdit)
        return;

    m_lineEdit->setText(QString::number(value));
}

void DirectusAdjustLabel::RepositionMouseOnScreenEdge()
{
    QPoint mousePos = QCursor::pos();
    QRect screen = QApplication::desktop()->screenGeometry();

    if (mousePos.x() == 0)
    {
        QPoint newMousePos = QPoint(screen.width(), mousePos.y());
        QCursor::setPos(newMousePos);
        m_lastMousePos = GetMousePosLocal().x();
    }

    if (mousePos.x() == screen.width() - 1)
    {
        QPoint newMousePos = QPoint(0, mousePos.y());
        QCursor::setPos(newMousePos);
        m_lastMousePos = GetMousePosLocal().x();
    }
}

float DirectusAdjustLabel::CalculateDelta()
{
    float mousePosX = GetMousePosLocal().x();
    float mouseDelta = mousePosX - m_lastMousePos;
    m_lastMousePos = mousePosX;

    return mouseDelta;
}

void DirectusAdjustLabel::Adjust()
{
    if (!m_isMouseDragged)
        return;

    m_mouseDelta = CalculateDelta();
    RepositionMouseOnScreenEdge();

    // Calculate the new texBox value
    m_currentTexBoxValue += m_mouseDelta * m_sensitivity;

    // Update the QLineEdit
    SetTextBoxValue(m_currentTexBoxValue);
}
