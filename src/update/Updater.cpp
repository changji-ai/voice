#include "Updater.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSysInfo>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QDeadlineTimer>
#include <QRegularExpression>
#include <cstdio>

#ifndef VOICE_VERSION
#define VOICE_VERSION "0.0.0"
#endif
#ifndef VOICE_REPO
#define VOICE_REPO "changji-ai/voice"
#endif

static QString stripV(QString s)
{
    if (s.startsWith('v') || s.startsWith('V'))
        s.remove(0, 1);
    return s;
}

static QVector<int> parseVer(const QString &s)
{
    QVector<int> out;
    const auto parts = stripV(s).split('.');
    for (const QString &p : parts)
        out.append(p.section(QRegularExpression("[^0-9]"), 0, 0).toInt());
    while (out.size() < 3)
        out.append(0);
    return out;
}

QString Updater::currentVersion()
{
    return QStringLiteral(VOICE_VERSION);
}

bool Updater::isNewer(const QString &remoteTag, const QString &current)
{
    const QVector<int> a = parseVer(remoteTag);
    const QVector<int> b = parseVer(current);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; i++) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

QString Updater::platformKey()
{
#ifdef Q_OS_WIN
    const QString os = "win";
#else
#ifdef Q_OS_MACOS
    const QString os = "mac";
#else
    const QString os = "linux";
#endif
#endif
    const QString cpu = QSysInfo::currentCpuArchitecture(); // x86_64 / arm64 / i386
    QString arch = cpu == "x86_64" ? "x64" : (cpu == "arm64" ? "arm64" : cpu);
    return os + "-" + arch;
}

Updater::Updater(QObject *parent) : QObject(parent), nam_(new QNetworkAccessManager(this)) { }

void Updater::checkNow()
{
    QNetworkRequest req{ QUrl("https://api.github.com/repos/" VOICE_REPO "/releases/latest") };
    req.setRawHeader("User-Agent", "VoiceAssistant-updater");
    req.setRawHeader("Accept", "application/vnd.github+json");
    // 可选：设置 VA_GITHUB_TOKEN 环境变量可绕过匿名 API 限流
    if (const QByteArray tok = qEnvironmentVariable("VA_GITHUB_TOKEN").toUtf8(); !tok.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + tok);
    auto *reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto root = doc.object();
        const QString tag = root.value("tag_name").toString();
        if (tag.isEmpty()) {
            emit checkFailed("还没有任何发布版本");
            return;
        }
        latestVersion_ = stripV(tag);
        latestNotes_ = root.value("body").toString();
        // 找本平台资产：VoiceAssistant-<ver>-<os>-<arch>.zip
        const QString want = "-" + platformKey() + ".zip";
        const auto assets = root.value("assets").toArray();
        for (const auto &a : assets) {
            const auto obj = a.toObject();
            const QString name = obj.value("name").toString();
            if (name.endsWith(want)) {
                latestUrl_ = obj.value("browser_download_url").toString();
                latestSize_ = obj.value("size").toInteger();
                break;
            }
        }
        if (isNewer(tag, currentVersion())) {
            if (latestUrl_.isEmpty())
                emit checkFailed("发现新版本 v" + latestVersion_ + "，但没有本平台（" + platformKey() + "）的安装包");
            else
                emit updateAvailable();
        } else {
            emit upToDate(latestVersion_);
        }
    });
}

void Updater::downloadAndInstall(const QString &url)
{
    QNetworkRequest req{ QUrl(url) };
    req.setRawHeader("User-Agent", "VoiceAssistant-updater");
    auto *reply = nam_->get(req);
    const QString zipPath = QDir::temp().absoluteFilePath("VoiceAssistant-update.zip");
    QFile::remove(zipPath);
    QFile *out = new QFile(zipPath, reply);
    if (!out->open(QIODevice::WriteOnly)) {
        emit downloadFailed("无法创建临时文件");
        reply->deleteLater();
        return;
    }
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 done, qint64 total) { emit downloadProgress(done, total); });
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] {
        out->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, zipPath] {
        out->flush();
        out->close();
        out->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(zipPath);
            emit downloadFailed(reply->errorString());
            return;
        }
        zipPath_ = zipPath;
        QString err;
        if (!prepareInstall(zipPath_))
            emit downloadFailed("解压或准备安装失败");
        else
            emit readyToRestart();
        Q_UNUSED(err)
    });
}

bool Updater::extractZip(const QString &zip, const QString &destDir, QString *err)
{
    QDir().mkpath(destDir);
    QProcess p;
#ifdef Q_OS_WIN
    // Windows 10 1803+ 自带 bsdtar，可直接解 zip
    p.start("tar", { "-xf", zip, "-C", destDir });
#else
    p.start("unzip", { "-o", zip, "-d", destDir });
#endif
    if (!p.waitForStarted(5000)) {
        *err = "无法启动解压工具";
        return false;
    }
    if (!p.waitForFinished(120000)) {
        p.kill();
        *err = "解压超时";
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *err = QString::fromUtf8(p.readAll());
        return false;
    }
    return true;
}

bool Updater::prepareInstall(const QString &zipPath)
{
    // 解压到临时目录（跨进程存活，重启脚本用完自删）
    const QString base = QDir::temp().absoluteFilePath("va-update");
    QDir(base).removeRecursively();
    extractDir_ = base + "/new";
    QString err;
    if (!extractZip(zipPath, extractDir_, &err))
        return false;
    QFile::remove(zipPath); // 安装包用不上了

#ifdef Q_OS_WIN
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString bat = base + "/apply.bat";
    QFile f(bat);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray batText = QStringLiteral(
        "@echo off\r\n"
        ":waitloop\r\n"
        "tasklist /FI \"PID eq %1\" | find \"%1\" >nul 2>&1\r\n"
        "if not errorlevel 1 (\r\n"
        "  timeout /t 1 /nobreak >nul\r\n"
        "  goto waitloop\r\n"
        ")\r\n"
        "xcopy /E /Y /I /Q \"%NEW%\\*\" \"%APP%\\\" >nul\r\n"
        "start \"\" \"%APP%\\VoiceAssistant.exe\"\r\n"
        "rd /S /Q \"%ROOT%\" >nul 2>&1\r\n")
        .replace("%NEW%", extractDir_)
        .replace("%APP%", appDir)
        .replace("%ROOT%", base)
        .toLocal8Bit();
    f.write(batText);
    f.close();
    scriptPath_ = bat;
#else
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString sh = base + "/apply.sh";
    QFile f(sh);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
#ifdef Q_OS_MACOS
    // appDir = xxx/VoiceAssistant.app/Contents/MacOS
    const QString bundle = QDir(appDir + "/../../..").absolutePath();
    const QString parent = QFileInfo(bundle).absolutePath();
    const QString body = QStringLiteral(
        "#!/bin/sh\n"
        "while kill -0 \"$1\" 2>/dev/null; do sleep 1; done\n"
        "rm -rf '%BUNDLE%'\n"
        "cp -R '%NEW%/VoiceAssistant.app' '%BUNDLE%' || true\n"
        "open -n '%BUNDLE%'\n"
        "rm -rf '%BASE%'\n")
        .replace("%BUNDLE%", bundle)
        .replace("%NEW%", extractDir_)
        .replace("%BASE%", base);
#else
    const QString body = QStringLiteral(
        "#!/bin/sh\n"
        "while kill -0 \"$1\" 2>/dev/null; do sleep 1; done\n"
        "cp -R '%NEW%'/. '%APP%'/ 2>/dev/null\n"
        "chmod +x '%APP%/VoiceAssistant'\n"
        "nohup '%APP%/VoiceAssistant' >/dev/null 2>&1 &\n"
        "rm -rf '%BASE%'\n")
        .replace("%NEW%", extractDir_)
        .replace("%APP%", appDir)
        .replace("%BASE%", base);
#endif
    f.write(body.toUtf8());
    f.close();
    scriptPath_ = sh;
#endif
    return true;
}

bool Updater::restartNow(QString *err)
{
    if (scriptPath_.isEmpty()) {
        if (err)
            *err = "没有待安装的更新";
        return false;
    }
    const qint64 pid = QCoreApplication::applicationPid();
    bool ok;
#ifdef Q_OS_WIN
    ok = QProcess::startDetached("cmd", { "/c", scriptPath_, QString::number(pid) });
#else
    ok = QProcess::startDetached("sh", { scriptPath_, QString::number(pid) });
#endif
    if (!ok && err)
        *err = "无法启动安装脚本";
    return ok;
}
