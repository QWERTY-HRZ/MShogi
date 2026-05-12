#include <QApplication>
#include <QFile>
#include <QTextStream>
// 全局抗锯齿字体
#include <QFont>
#include "UIController.h"

int main(int argc, char *argv[]) {
    // Qt 6 原生全面支持高分屏与缩放
    QApplication app(argc, argv);

    // 设置全局抗锯齿字体
    QFont globalFont("Microsoft YaHei");
    // 强制开启底层 ClearType 抗锯齿平滑渲染
    globalFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(globalFont);
    
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
