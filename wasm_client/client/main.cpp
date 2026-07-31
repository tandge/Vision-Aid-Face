/**
 * @file main.cpp
 * @brief 应用程序入口
 *
 * BntechEyeFriend - 摄像头实时画面与语音交互界面
 * 基于 Qt 6.6 构建，支持跨平台运行
 */

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("BntechEyeFriend");
    app.setApplicationVersion("1.0.0");

#ifdef Q_OS_WASM
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansSC-VF.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            QFont font(families.first());
            font.setStyleStrategy(QFont::PreferAntialias);
            app.setFont(font);
        }
    } else {
        qWarning() << "Failed to load bundled Chinese font";
    }
#endif

    MainWindow window;
    window.show();

    return app.exec();
}
