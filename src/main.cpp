#include <QApplication>
#include "UIController.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    UIController window;
    window.show();
    
    return app.exec();
}