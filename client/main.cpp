#include <QApplication>
#include "FereastraPrincipala.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    FereastraPrincipala fp;
    fp.show();

    return app.exec();
}
