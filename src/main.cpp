#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "UIController.h"

int main(int argc, char *argv[]) {
    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // 开启高分屏适配与高清图标渲染 (Qt 5 需手动开启)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    #endif
    QApplication app(argc, argv);
    
    QFile styleFile(":/res/style.qss");
    if(styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&styleFile);
        // 指定QSS解析格式
        ts.setCodec("UTF-8");
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
