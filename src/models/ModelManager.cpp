#include "ModelManager.h"
#include "util/Settings.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QSaveFile>

QVector<ModelDef> ModelManager::builtinModels()
{
    return {
        // 实时 STT：中英双语流式 zipformer（ModelScope，只下 int8 5 个文件 ≈ 200MB）
        ModelDef{ "zipformer-zh-en", "识别·流式 Zipformer 中英双语",
                  "stt", "modelscope",
                  "pkufool/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20",
                  { "encoder-epoch-99-avg-1.int8.onnx", "decoder-epoch-99-avg-1.int8.onnx",
                    "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt", "bpe.model" },
                  200ull * 1024 * 1024 },
        // TTS：kokoro 中英多音色（hf-mirror，fp32 310MB + espeak-ng-data/dict）
        ModelDef{ "kokoro-multi-lang", "合成·Kokoro 中英 53 音色",
                  "tts", "hf",
                  "csukuangfj/kokoro-multi-lang-v1_0",
                  { "model.onnx", "voices.bin", "tokens.txt", "lexicon-us-en.txt", "lexicon-zh.txt",
                    "date-zh.fst", "number-zh.fst", "phone-zh.fst",
                    "espeak-ng-data/", "dict/" },
                  400ull * 1024 * 1024 },
    };
}

ModelDef ModelManager::find(const QString &key)
{
    for (const auto &m : builtinModels())
        if (m.key == key)
            return m;
    return ModelDef{};
}

// 就绪判断：下载完成后写入 done.marker
bool ModelManager::isReady(const ModelDef &def)
{
    const QString dir = modelsRoot() + "/" + def.key;
    return QFile::exists(dir + "/done.marker");
}

void ModelManager::download(const ModelDef &def)
{
    def_ = def;
    files_.clear();
    fileIdx_ = 0;
    doneBytes_ = 0;
    totalBytes_ = 0;
    canceled_ = false;
    fetchFileList();
}

void ModelManager::cancel()
{
    canceled_ = true;
}

static bool wanted(const ModelDef &def, const QString &path)
{
    if (path.endsWith(".md", Qt::CaseInsensitive) || path == ".gitattributes" || path == "LICENSE"
        || path.endsWith(".py"))
        return false;
    if (path == "model.onnx") // kokoro 只用 int8
        return false;
    if (def.files.isEmpty())
        return true;
    for (const QString &f : def.files) {
        if (f.endsWith('/')) {
            if (path.startsWith(f))
                return true;
        } else if (path == f) {
            return true;
        }
    }
    return false;
}

QString ModelManager::downloadUrl(const QString &path) const
{
    if (def_.host == "hf")
        return QString("https://hf-mirror.com/%1/resolve/main/%2")
            .arg(def_.repo, QString::fromUtf8(QUrl::toPercentEncoding(path)));
    return QString("https://modelscope.cn/api/v1/models/%1/repo?FilePath=%2")
        .arg(def_.repo, QString::fromUtf8(QUrl::toPercentEncoding(path)));
}

static QNetworkRequest reqWithUA(const QUrl &url)
{
    QNetworkRequest req{ url };
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) voice-assistant/1.0");
    return req;
}

void ModelManager::fetchFileList()
{
    QNetworkReply *reply = nullptr;
    if (def_.host == "hf") {
        // HF 镜像：?blobs=true 带文件大小
        const QString url = QString("https://hf-mirror.com/api/models/%1?blobs=true").arg(def_.repo);
        reply = nam_.get(reqWithUA(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (canceled_) {
                emit finished(false, "已取消");
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                emit finished(false, "获取模型文件列表失败: " + reply->errorString());
                return;
            }
            const QJsonArray siblings =
                QJsonDocument::fromJson(reply->readAll()).object()["siblings"].toArray();
            for (const auto &s : siblings) {
                const QJsonObject so = s.toObject();
                const QString p = so["rfilename"].toString();
                if (!wanted(def_, p))
                    continue;
                FileEntry e;
                e.path = p;
                e.size = qint64(so["size"].toDouble());
                files_.append(e);
                totalBytes_ += e.size;
            }
            if (files_.isEmpty()) {
                emit finished(false, "模型仓库文件列表为空");
                return;
            }
            downloadNextFile();
        });
    } else {
        // ModelScope
        const QString url =
            QString("https://modelscope.cn/api/v1/models/%1/repo/files?Recursive=true").arg(def_.repo);
        reply = nam_.get(reqWithUA(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (canceled_) {
                emit finished(false, "已取消");
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                emit finished(false, "获取模型文件列表失败: " + reply->errorString());
                return;
            }
            const QJsonArray arr =
                QJsonDocument::fromJson(reply->readAll()).object()["Data"].toObject()["Files"].toArray();
            for (const auto &f : arr) {
                const QJsonObject fo = f.toObject();
                if (fo["Type"].toString() != "blob")
                    continue;
                const QString p = fo["Path"].toString();
                if (!wanted(def_, p))
                    continue;
                FileEntry e;
                e.path = p;
                e.size = qint64(fo["Size"].toDouble());
                files_.append(e);
                totalBytes_ += e.size;
            }
            if (files_.isEmpty()) {
                emit finished(false, "模型仓库文件列表为空");
                return;
            }
            downloadNextFile();
        });
    }
}

void ModelManager::downloadNextFile()
{
    if (canceled_) {
        emit finished(false, "已取消");
        return;
    }
    if (fileIdx_ >= files_.size()) {
        QDir().mkpath(modelsRoot() + "/" + def_.key);
        QFile marker(modelsRoot() + "/" + def_.key + "/done.marker");
        marker.open(QIODevice::WriteOnly);
        marker.write("ok");
        marker.close();
        emit finished(true, QString());
        return;
    }
    const FileEntry &e = files_[fileIdx_];
    const QString absPath = modelsRoot() + "/" + def_.key + "/" + e.path;
    if (QFile::exists(absPath) && QFileInfo(absPath).size() == e.size && e.size > 0) {
        doneBytes_ += e.size;
        fileIdx_++;
        downloadNextFile();
        return;
    }
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    auto *reply = nam_.get(reqWithUA(QUrl(downloadUrl(e.path))));
    auto *out = new QSaveFile(absPath, this);
    out->open(QIODevice::WriteOnly);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, e](qint64 recv, qint64) {
        const int percent =
            totalBytes_ > 0 ? int(100.0 * (doneBytes_ + qMin(recv, e.size)) / totalBytes_) : 0;
        emit progress(percent, e.path);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, e] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || canceled_) {
            out->cancelWriting();
            out->deleteLater();
            emit finished(false, "下载失败: " + e.path + " " + reply->errorString());
            return;
        }
        out->commit();
        out->deleteLater();
        doneBytes_ += e.size;
        fileIdx_++;
        downloadNextFile();
    });
}
