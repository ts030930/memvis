#include "memvis.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MemVis window;
    window.show();
    return app.exec();
}
