// INCLUDES ====================
#include "DirectusAdjustLabel.h"
#include <QMouseEvent>
#include "IO/Log.h"
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

    // More about cursor icons:
    // http://doc.qt.io/qt-5/qcursor.html

    // Forward the original event
    QLabel::mouseMoveEvent(event);

     if(event->buttons() == Qt::LeftButton)
     {
        CalculateDelta();
        Adjust();
     }
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

void DirectusAdjustLabel::CalculateDelta()
{
    QPoint mousePos = QCursor::pos();
    QPoint labelPos = this->geometry().center();
    labelPos = QWidget::mapToGlobal(labelPos);

    float x1 = mousePos.x();
    float x2 = labelPos.x();
    float x = x2 - x1;

    m_delta = m_x - x;
    m_x = x;
}

void DirectusAdjustLabel::Adjust()
{
    if (!m_lineEdit)
        return;

    float currentValue = m_lineEdit->text().toFloat();
    float newValue = currentValue + m_delta;
    m_lineEdit->setText(QString::number(newValue));
}
