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

#pragma once

// INCLUDES ===============
#include <QLabel>
#include <QLineEdit>
//=========================

class DirectusAdjustLabel : public QLabel
{
    Q_OBJECT
public:
    explicit DirectusAdjustLabel(QWidget* parent = 0);
    void AdjustQLineEdit(QLineEdit* lineEdit);
protected:
    // mouseMoveEvent() is called whenever the
    // mouse moves while a mouse button is held down.
    // This can be useful during drag and drop operations.
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void leaveEvent(QEvent* event);

private:
    void RepositionMouseOnScreenEdge(QPoint mousePos);
    float CalculateDelta(QPoint mousePos, QPoint labelPos);
    void Adjust();

    QLineEdit* m_lineEdit;
    bool m_isMouseHovering;
    bool m_isMouseDragged;
    float m_x;
    float m_delta;

signals:

public slots:
};
