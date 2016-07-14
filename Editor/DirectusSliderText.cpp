//=============================
#include "DirectusSliderText.h"
#include "IO/Log.h"
//=============================

DirectusSliderText::DirectusSliderText(QWidget* parent) : QWidget(parent)
{
    m_slider = nullptr;
    m_lineEdit = nullptr;
}

void DirectusSliderText::Initialize(int min, int max)
{
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(min * 100, max * 100);
    m_slider->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    m_lineEdit = new QLineEdit();
    m_lineEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_lineEdit->setValidator(m_validator);

    connect(m_slider, SIGNAL(valueChanged(int)), this, SLOT(UpdateFromSlider()));
    connect(m_lineEdit, SIGNAL(textEdited(QString)), this, SLOT(UpdateFromLineEdit()));
}

void DirectusSliderText::SetValue(float value)
{
    m_lineEdit->setText(QString::number(value));
    m_slider->setValue(value * 100);
}

float DirectusSliderText::GetValue()
{
   return m_lineEdit->text().toFloat();
}

QSlider* DirectusSliderText::GetSlider()
{
    return m_slider;
}

QLineEdit* DirectusSliderText::GetLineEdit()
{
    return m_lineEdit;
}

void DirectusSliderText::UpdateFromSlider()
{
    float value = (float)m_slider->value() / 100;
    m_lineEdit->setText(QString::number(value));

    emit valueChanged(value);
}

void DirectusSliderText::UpdateFromLineEdit()
{
    float value = m_lineEdit->text().toFloat();
    m_slider->setValue(value * 100);

    emit valueChanged(value);
}
