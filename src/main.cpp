#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "UIController.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    QFile styleFile(":/res/style.qss");
    if(styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&styleFile);
        app.setStyleSheet(ts.readAll());
        styleFile.close();
    } else {
        // 在控制台打印报错
        qWarning("Cannot open QSS file!");
    }

    UIController window;
    window.show();
    
    return app.exec();
}
