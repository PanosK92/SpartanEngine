/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =======================
#include "DirectusComboSliderText.h"
#include "Logging/Log.h"
//==================================

DirectusComboSliderText::DirectusComboSliderText(QWidget* parent) : QWidget(parent)
{
    m_slider = nullptr;
    m_lineEdit = nullptr;
}

void DirectusComboSliderText::Initialize(int min, int max)
{
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);
    m_validator->setProperty("notation", QDoubleValidator::StandardNotation);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(min * 100, max * 100);
    m_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_lineEdit = new QLineEdit();
    m_lineEdit->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_lineEdit->setValidator(m_validator);

    // textChanged(QString) -> emits signal when changed through code
    // textEdited(QString) -> doesn't emits signal when changed through code
    connect(m_slider, SIGNAL(valueChanged(int)), this, SLOT(UpdateFromSlider()));
    connect(m_lineEdit, SIGNAL(textEdited(QString)), this, SLOT(UpdateFromLineEdit()));
}

void DirectusComboSliderText::SetValue(float value)
{
    m_lineEdit->setText(QString::number(value));
    m_slider->setValue(value * 100);
}

float DirectusComboSliderText::GetValue()
{
   return m_lineEdit->text().toFloat();
}

QSlider* DirectusComboSliderText::GetSlider()
{
    return m_slider;
}

QLineEdit* DirectusComboSliderText::GetLineEdit()
{
    return m_lineEdit;
}

void DirectusComboSliderText::UpdateFromSlider()
{
    float value = (float)m_slider->value() / 100;
    m_lineEdit->setText(QString::number(value));

    emit ValueChanged();
}

void DirectusComboSliderText::UpdateFromLineEdit()
{
    float value = m_lineEdit->text().toFloat();
    m_slider->setValue(value * 100);

    emit ValueChanged();
}
