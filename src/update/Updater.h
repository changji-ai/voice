#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// 应用内自动更新：
// 1) 查 GitHub Releases 最新版本（比较语义化版本号）
// 2) 下载对应平台 zip（带进度）
// 3) 解压到临时目录，写一个「等旧进程退出 → 覆盖安装目录 → 重启应用」的脚本
// 4) 退出当前进程，脚本完成静默安装并启动新版
class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(QObject *parent = nullptr);

    static QString currentVersion();                  // 编译期 VOICE_VERSION
    static bool isNewer(const QString &remoteTag, const QString &current);
    static QString platformKey();                     // 如 "win-x64" / "mac-arm64"
    static qint64 assetSizeHint() { return 0; }

    void checkNow();                                  // 异步查最新版
    void downloadAndInstall(const QString &url);      // 异步下载，进度走信号
    bool restartNow(QString *err);                    // 下载解压完成后调用：执行替换脚本并让调用方退出

    QString latestVersion() const { return latestVersion_; }
    QString latestUrl() const { return latestUrl_; }
    qint64 latestSize() const { return latestSize_; }
    QString latestNotes() const { return latestNotes_; }
    QString downloadedZip() const { return zipPath_; }

signals:
    void upToDate(const QString &version);
    void updateAvailable();
    void checkFailed(const QString &error);
    void downloadProgress(qint64 done, qint64 total);
    void downloadFailed(const QString &error);
    void readyToRestart(); // zip 已解压、脚本已就绪，调 restartNow() 后退出即可

private:
    bool extractZip(const QString &zip, const QString &destDir, QString *err);
    bool prepareInstall(const QString &zipPath);

    QNetworkAccessManager *nam_;
    QString latestVersion_, latestUrl_, latestNotes_;
    qint64 latestSize_ = 0;
    QString zipPath_;
    QString scriptPath_; // 替换+重启脚本
    QString extractDir_;
};
