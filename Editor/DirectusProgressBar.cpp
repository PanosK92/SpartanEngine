#include "DirectusProgressBar.h"
#include "ui_AssetLoadingDialog.h"

DirectusProgressBar::DirectusProgressBar(QWidget *parent) : QDialog(parent), ui(new Ui::AssetLoadingDialog)
{
    ui->setupUi(this);
    m_mainWindow = nullptr;

    // WINDOW FLAGS - How the window appears
    Qt::WindowFlags flags = windowFlags() | Qt::MSWindowsFixedSizeDialogHint;
    Qt::WindowFlags helpFlag = Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint;
    flags = flags & (~helpFlag);
       setWindowFlags(flags);

    // How fast the progress bar... progresses
    m_timer = new QTimer(this);
    m_timer->start(20);

    connect(m_timer, SIGNAL(timeout()), this, SLOT(UpdateProgressBar()));
}

void DirectusProgressBar::Initialize(QWidget* mainWindow)
{
    m_mainWindow = mainWindow;
}

void DirectusProgressBar::UpdateProgressBar()
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

void DirectusProgressBar::Show()
{
    m_mainWindow->children();
    m_mainWindow->setEnabled(false);
    show();
}

void DirectusProgressBar::Hide()
{
    m_mainWindow->setEnabled(true);
    ui->progressBar->setValue(0);
    hide();
}

void DirectusProgressBar::Kill()
{
    m_mainWindow->setEnabled(true);
    deleteLater();
}
