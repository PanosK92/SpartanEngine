#pragma once

#include <QDialog>
#include <QTimer>

namespace Ui {
class AssetLoadingDialog;
}

class AssetLoadingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssetLoadingDialog(QWidget *parent = 0);
    void Initialize(QWidget* mainWindow);

private slots:
    void UpdateProgressBar();

public slots:
    void Show();
    void Hide();
    void Kill();

private:
    Ui::AssetLoadingDialog *ui;
    QTimer* m_timer;
    QWidget* m_mainWindow;
};
