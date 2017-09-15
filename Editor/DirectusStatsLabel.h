#pragma once

//= INCLUDES =======
#include <QLineEdit>
//==================

class DirectusViewport;

class DirectusStatsLabel : public QLineEdit
{
    Q_OBJECT
public:
    explicit DirectusStatsLabel(QWidget *parent = 0);
    void UpdateStats(DirectusViewport* directusViewport);
    std::string FormatFloat(float value, int digitsAfterDecimal);
    std::string FormatDouble(double value, int digitsAfterDecimal);
};
