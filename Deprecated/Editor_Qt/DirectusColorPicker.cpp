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
#include "Logging/Log.h"
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

    m_colorDialog = new QColorDialog(m_mainWindow);
    m_colorDialog->setWindowTitle("Color");
    m_colorDialog->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::ShowAlphaChannel);
    m_colorDialog->setStyleSheet(
                "QPushButton"
                "{"
                    "background-color: qlineargradient(spread:pad, x1:0.5, y1:1, x2:0.5, y2:0, stop:0 rgba(92, 92, 92, 100), stop:1 rgba(92, 92, 92, 255));"
                    "border-radius: 2px;"
                    "border-color: #575757;"
                    "border-width: 1px;"
                    "border-style: solid;"
                "}"
                "QPushButton:pressed"
                "{"
                "background-color: qlineargradient(spread:pad, x1:0.5, y1:1, x2:0.5, y2:0, stop:0 rgba(92, 92, 92, 255), stop:1 rgba(92, 92, 92, 100));"
                "}"
                "QLabel"
                "{"
                    "padding-left: 3px;"
                "}"
                );

    connect(m_button, SIGNAL(pressed()), this, SLOT(ShowColorPickerWindow()));
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

void DirectusColorPicker::ShowColorPickerWindow()
{
    emit ColorPickingStarted();

    m_colorDialog->setCurrentColor(QColor(m_color.x * 255.0f, m_color.y * 255.0f, m_color.z * 255.0f, m_color.w * 255.0f));
    m_colorDialog->show();

    connect(m_colorDialog, SIGNAL(accepted()), this, SLOT(AcceptColorPicking()));
    connect(m_colorDialog, SIGNAL(rejected()), this, SLOT(RejectColorPicking()));

    emit ColorPickingCompleted();
}

void DirectusColorPicker::AcceptColorPicking()
{
    QColor color = m_colorDialog->selectedColor();
    Vector4 colorV4 = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());

    // Set the color of the widget as the selected color.
    SetColor(colorV4);

    emit ColorPickingCompleted();
}

void DirectusColorPicker::RejectColorPicking()
{
    emit ColorPickingRejected();
}
