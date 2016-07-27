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

// INCLUDES ====================
#include "DirectusColorPicker.h"
#include "IO/Log.h"
#include <QColorDialog>
//==============================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

DirectusColorPicker::DirectusColorPicker(QWidget *parent) : QWidget(parent)
{

}

void DirectusColorPicker::Initialize(QWidget* mainWindow)
{
    m_mainWindow = mainWindow;
    m_button = new QPushButton();

    connect(m_button, SIGNAL(pressed()), this, SLOT(GetColorFromColorPicker()));
}

Vector4 DirectusColorPicker::GetColor()
{
    return m_color;
}

void DirectusColorPicker::SetColor(Vector4 color)
{
    m_color = color;
    string r = to_string(color.x * 255.0f);
    string g = to_string(color.y * 255.0f);
    string b = to_string(color.z * 255.0f);
    string a = to_string(color.w * 255.0f);
    string rgba = "background-color: rgb(" + r + "," + g + "," + b + "," + a + ");";

    m_button->setStyleSheet(QString::fromStdString(rgba));
}

QPushButton* DirectusColorPicker::GetWidget()
{
    return m_button;
}

void DirectusColorPicker::GetColorFromColorPicker()
{
    emit ColorPickingStarted();

    const QColorDialog::ColorDialogOptions options = QColorDialog::DontUseNativeDialog | QColorDialog::ShowAlphaChannel;
    QColor originalColor = QColor(m_color.x * 255.0f, m_color.y * 255.0f, m_color.z * 255.0f, m_color.w * 255.0f);
    QColor newColor = QColorDialog::getColor(originalColor, m_mainWindow, "Select Color", options);

    emit ColorPickingCompleted();

    Vector4 color = Vector4(newColor.redF(), newColor.greenF(), newColor.blueF(), newColor.alphaF());
    SetColor(color);
}
