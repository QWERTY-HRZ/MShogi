#include <QApplication>
#include <QFile>
#include <QTextStream>
// 全局抗锯齿字体
#include <QFont>
#include "UIController.h"

int main(int argc, char *argv[]) {
    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // 开启高分屏适配与高清图标渲染 (Qt 5 需手动开启)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    #endif
    #endif
    QApplication app(argc, argv);

    // 设置全局抗锯齿字体
    QFont globalFont("Microsoft YaHei");
    // 强制开启底层 ClearType 抗锯齿平滑渲染
    globalFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(globalFont);
    
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
