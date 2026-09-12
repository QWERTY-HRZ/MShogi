#include <QApplication>
#include <QFile>
#include <QTextStream>
// 全局抗锯齿字体
#include <QFont>
#include <QDir>
#include "UIController.h"
#ifdef MSHOGI_WITH_ONNX_AGENT
#include "OnnxAgent.h"
#include "SelfPlay.h"
#endif

int main(int argc, char *argv[]) {
    // Qt 6 原生全面支持高分屏与缩放
    QApplication app(argc, argv);

    if (app.arguments().contains("--ai-smoke")) {
#ifdef MSHOGI_WITH_ONNX_AGENT
        try {
            const QDir appDirectory(QCoreApplication::applicationDirPath());
            OnnxAgent agent(
                appDirectory.filePath("onnxruntime.dll").toStdWString(),
                appDirectory.filePath("models/mshogi_policy_value.onnx").toStdWString());
            GameCore core;
            const auto selected = agent.chooseAction(core);
            // 固定初始动作同时核对部署模型、状态编码和 top-5 价值重排。
            return selected && encodeAction(*selected) == 833 ? 0 : 2;
        } catch (const std::exception& error) {
            qCritical("AI smoke test failed: %s", error.what());
            return 3;
        }
#else
        return 4;
#endif
    }

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
