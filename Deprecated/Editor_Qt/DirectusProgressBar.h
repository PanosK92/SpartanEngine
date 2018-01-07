#pragma once

//= INCLUDES =====
#include <QDialog>
//================

class QTimer;

namespace Ui
{
class AssetLoadingDialog;
}

namespace Directus
{
    class Context;
}

class DirectusProgressBar : public QDialog
{
    Q_OBJECT

public:
    explicit DirectusProgressBar(QWidget* parent = 0);
    void Initialize(QWidget* mainWindow, Directus::Context* engineContext);

private slots:
    void IncrementTowardsTargetValue();
    void UpdateProgressBar();

public slots:
    void Show();
    void Hide();
    void Kill();

private:
    bool m_isVisible;
    int m_min;
    int m_max;
    int m_targetValue;

    Ui::AssetLoadingDialog *ui;
    QTimer* m_timerProgressUpdate;
    QTimer* m_timerSmoothBar;
    QWidget* m_mainWindow;
    Directus::Context* m_engineContext;
};
