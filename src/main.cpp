#include "MainWindow.h"
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <cstdlib>

int g_openTab = -1;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("VoiceAssistant");
    app.setApplicationName("VoiceAssistant");

    MainWindow w;
    w.show();

    // 调试入口：--settings [--tab N] 启动后自动打开设置对话框
    for (int i = 1; i < argc; i++) {
        if (qstrcmp(argv[i], "--settings") == 0)
            QTimer::singleShot(600, &w, &MainWindow::openSettings);
        else if (qstrcmp(argv[i], "--tab") == 0 && i + 1 < argc)
            g_openTab = atoi(argv[++i]);
    }

    return app.exec();
}
