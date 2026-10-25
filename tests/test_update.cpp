// 自动更新端到端验收（无 GUI）：
// 检查最新版 → 有新版则下载 → 解压 → 拉起替换脚本 → 本进程退出
// 脚本等本进程退出后覆盖 build/bin 并启动新版 VoiceAssistant.exe
#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
#include "update/Updater.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    auto *u = new Updater(&app);
    QObject::connect(u, &Updater::upToDate, &app, [&out](const QString &v) {
        out << "UP-TO-DATE " << v << Qt::endl;
        qApp->quit();
    });
    QObject::connect(u, &Updater::checkFailed, &app, [&out](const QString &e) {
        out << "CHECK-FAILED " << e << Qt::endl;
        qApp->quit();
    });
    QObject::connect(u, &Updater::updateAvailable, &app, [u, &out] {
        out << "UPDATE-AVAILABLE v" << u->latestVersion()
            << " size=" << u->latestSize() << Qt::endl;
        u->downloadAndInstall(u->latestUrl());
    });
    QObject::connect(u, &Updater::downloadProgress, &app, [&out](qint64 d, qint64 t) {
        out << "PROGRESS " << d << "/" << t << Qt::endl;
    });
    QObject::connect(u, &Updater::downloadFailed, &app, [&out](const QString &e) {
        out << "DOWNLOAD-FAILED " << e << Qt::endl;
        qApp->quit();
    });
    QObject::connect(u, &Updater::readyToRestart, &app, [u, &out] {
        out << "READY-TO-RESTART" << Qt::endl;
        QString err;
        if (u->restartNow(&err)) {
            out << "RESTART-SCRIPT-LAUNCHED, exiting" << Qt::endl;
            qApp->quit();
        } else {
            out << "RESTART-FAILED " << err << Qt::endl;
            qApp->quit();
        }
    });
    QTimer::singleShot(0, u, &Updater::checkNow);
    QTimer::singleShot(600000, &app, &QCoreApplication::quit); // 兜底 10 分钟
    return app.exec();
}
