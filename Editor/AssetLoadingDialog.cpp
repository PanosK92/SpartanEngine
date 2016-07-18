#include "AssetLoadingDialog.h"
#include "ui_AssetLoadingDialog.h"

AssetLoadingDialog::AssetLoadingDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AssetLoadingDialog)
{
    ui->setupUi(this);

    Qt::WindowFlags flags = windowFlags() | Qt::MSWindowsFixedSizeDialogHint;
    Qt::WindowFlags helpFlag = Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint;
    flags = flags & (~helpFlag);
       setWindowFlags(flags);

    m_mainWindow = nullptr;
    m_timer = new QTimer(this);
    m_timer->start(40);

    connect(m_timer, SIGNAL(timeout()), this, SLOT(UpdateProgressBar()));
}

void AssetLoadingDialog::SetMainWindow(QWidget* mainWindow)
{
    m_mainWindow = mainWindow;
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

void AssetLoadingDialog::Show()
{
    m_mainWindow->children();
    m_mainWindow->setEnabled(false);
    show();
}

void AssetLoadingDialog::Kill()
{
    m_mainWindow->setEnabled(true);
    deleteLater();
}
