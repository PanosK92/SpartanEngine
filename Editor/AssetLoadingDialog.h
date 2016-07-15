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
    ~AssetLoadingDialog();

public slots:
    void UpdateProgressBar();

private:
    Ui::AssetLoadingDialog *ui;
    QTimer* m_timer;
};
