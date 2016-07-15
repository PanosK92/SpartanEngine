#include "AssetLoadingDialog.h"
#include "ui_AssetLoadingDialog.h"

AssetLoadingDialog::AssetLoadingDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AssetLoadingDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Tool);

    m_timer = new QTimer(this);
    m_timer->start(40);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(UpdateProgressBar()));
}

AssetLoadingDialog::~AssetLoadingDialog()
{
    delete ui;
}

void AssetLoadingDialog::UpdateProgressBar()
{
    QProgressBar* progressBar = ui->progressBar;

    int value = progressBar->value();

    // If the bar reaches it's max value, reset
    if (value == progressBar->maximum())
        progressBar->setValue(progressBar->minimum());

    // Increment
    value += 1;
    ui->progressBar->setValue(value);
}
