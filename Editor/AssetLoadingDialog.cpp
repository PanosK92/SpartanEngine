#include "AssetLoadingDialog.h"
#include "ui_AssetLoadingDialog.h"

AssetLoadingDialog::AssetLoadingDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AssetLoadingDialog)
{
    ui->setupUi(this);
}

AssetLoadingDialog::~AssetLoadingDialog()
{
    delete ui;
}
