#pragma once

//= INCLUDES =======
#include <QLineEdit>
//==================

class DirectusCore;

class DirectusStatsLabel : public QLineEdit
{
    Q_OBJECT
public:
    explicit DirectusStatsLabel(QWidget *parent = 0);
    void UpdateStats(DirectusCore* directusCore);
    std::string FormatFloat(float value, int digitsAfterDecimal);
};
