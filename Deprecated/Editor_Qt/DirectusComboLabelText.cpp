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

//= INCLUDES ======================
#include "DirectusComboLabelText.h"
#include "DirectusAdjustLabel.h"
#include "Logging/Log.h"
//=================================

DirectusComboLabelText::DirectusComboLabelText(QWidget *parent) : QWidget(parent)
{
    m_label = nullptr;
    m_text = nullptr;
}

void DirectusComboLabelText::Initialize(QString labelText)
{
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);
    m_validator->setProperty("notation", QDoubleValidator::StandardNotation);

    // Initialize the text
    m_text = new QLineEdit();
    m_text->setValidator(m_validator);

    // Initialize the label
    m_label = new DirectusAdjustLabel();
    m_label->setText(labelText);
    m_label->setAlignment(Qt::AlignRight);
    m_label->Initialize(m_text);

    // textChanged(QString) -> emits signal when changed through code
    // textEdited(QString) -> doesn't emit signal when changed through code
    connect(m_label, SIGNAL(Adjusted()), this, SLOT(TextGotEdited()));
    connect(m_text, SIGNAL(textEdited(QString)), this, SLOT(TextGotEdited()));
}

void DirectusComboLabelText::AlignLabelToTheLeft()
{
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void DirectusComboLabelText::AlignLabelToTheRight()
{
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

DirectusAdjustLabel*DirectusComboLabelText::GetLabelWidget()
{
    return m_label;
}

QLineEdit*DirectusComboLabelText::GetTextWidget()
{
    return m_text;
}

float DirectusComboLabelText::GetAsFloat()
{
    return m_text->text().toFloat();
}

void DirectusComboLabelText::SetFromFloat(float value)
{
    m_text->setText(QString::number(value));
}

void DirectusComboLabelText::TextGotEdited()
{
    emit ValueChanged();
}
