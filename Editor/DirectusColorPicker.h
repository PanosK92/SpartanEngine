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

// INCLUDES =============
#include <QPushButton>
#include "Math/Vector4.h"
#include <QColorDialog>
//=======================

class DirectusColorPicker : public QWidget
{
    Q_OBJECT
public:
    explicit DirectusColorPicker(QWidget *parent = 0);
    void Initialize();

    Directus::Math::Vector4 GetColor();
    void SetColor(Directus::Math::Vector4 color);
    QPushButton* GetWidget();

private:
    QColorDialog* m_colorDialog;
    QPushButton* m_button;
    Directus::Math::Vector4 m_color;

signals:
    void ColorPicked();

private slots:
    void ShowColorPicker();
    void GetColorFromColorPicker(QColor color);
};
