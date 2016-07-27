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

// INCLUDES ==========================
#include "DirectusColorPicker.h"
#include "IO/Log.h"
//====================================

//=============================
using namespace Directus::Math;
//=============================

DirectusColorPicker::DirectusColorPicker(QWidget *parent) : QWidget(parent)
{

}

void DirectusColorPicker::Initialize()
{
    m_button = new QPushButton();
    m_colorDialog = new QColorDialog();

    connect(m_button, SIGNAL(pressed()), this, SLOT(ShowColorPicker()));
    connect(m_colorDialog, SIGNAL(colorSelected(QColor)), this, SLOT(GetColorFromColorPicker(QColor)));
}

Vector4 DirectusColorPicker::GetColor()
{
    return m_color;
}

void DirectusColorPicker::SetColor(Vector4 color)
{
    m_color = color;
}

QPushButton* DirectusColorPicker::GetWidget()
{
    return m_button;
}

void DirectusColorPicker::ShowColorPicker()
{
    m_colorDialog->getColor();
}

void DirectusColorPicker::GetColorFromColorPicker(QColor color)
{
    m_color = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    LOG("1");
    emit ColorPicked();
}
