#include "editor.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Editor window;
    window.show();

    return a.exec();
}
