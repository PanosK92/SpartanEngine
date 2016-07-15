#ifndef ASSETLOADINGDIALOG_H
#define ASSETLOADINGDIALOG_H

#include <QDialog>

namespace Ui {
class AssetLoadingDialog;
}

class AssetLoadingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssetLoadingDialog(QWidget *parent = 0);
    ~AssetLoadingDialog();

private:
    Ui::AssetLoadingDialog *ui;
};

#endif // ASSETLOADINGDIALOG_H
