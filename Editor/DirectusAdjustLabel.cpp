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
}

void DirectusAdjustLabel::AdjustQLineEdit(QLineEdit* lineEdit)
{
    m_lineEdit = lineEdit;
}

// The mouse cursor is hovering above the label
void DirectusAdjustLabel::mouseMoveEvent(QMouseEvent* event)
{
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

void DirectusAdjustLabel::RepositionMouseOnScreenEdge(QPoint mousePos)
{
    QRect screen = QApplication::desktop()->screenGeometry();
    if (mousePos.x() == 0)
    {
        QPoint newMousePos = QPoint(screen.width(), mousePos.y());
        QCursor::setPos(newMousePos);
    }

    if (mousePos.x() == screen.width() - 1)
    {
        QPoint newMousePos = QPoint(0, mousePos.y());
        QCursor::setPos(newMousePos);
    }
}

float DirectusAdjustLabel::CalculateDelta(QPoint mousePos, QPoint labelPos)
{
    float x1 = mousePos.x();
    float x2 = labelPos.x();
    float x = x2 - x1;

    m_delta = m_x - x;
    m_x = x;

    return m_delta;
}

void DirectusAdjustLabel::Adjust()
{
    if (!m_lineEdit || !m_isMouseDragged)
        return;

    // Aquire data
    QPoint mousePos = QCursor::pos();
    QPoint labelPos = this->geometry().center();
    labelPos = QWidget::mapToGlobal(labelPos);

    // Perform any actions needed on them
    CalculateDelta(mousePos, labelPos);
    RepositionMouseOnScreenEdge(mousePos);

    // Update the QLineEdit
    float currentValue = m_lineEdit->text().toFloat();
    float newValue = currentValue + m_delta;  
    m_lineEdit->setText(QString::number(newValue));
}
