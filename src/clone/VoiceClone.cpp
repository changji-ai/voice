#include "VoiceClone.h"
#include "util/Settings.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDateTime>
#include <QFileInfo>

QString VoiceClone::findFfmpeg(const AppSettings &cfg)
{
    // 1. 设置里指定的路径
    if (!cfg.ffmpegPath.isEmpty() && QFile::exists(cfg.ffmpegPath))
        return cfg.ffmpegPath;
    // 2. exe 旁 tools/ffmpeg.exe
    const QString beside = QCoreApplication::applicationDirPath() + "/tools/ffmpeg.exe";
    if (QFile::exists(beside))
        return beside;
    // 3. PATH
    return QStandardPaths::findExecutable("ffmpeg");
}

void VoiceClone::extractSample(const QString &mediaPath, const QString &outWav, std::function<void(bool, QString)> cb)
{
    const QString ffmpeg = findFfmpeg(*cfg_);
    if (ffmpeg.isEmpty()) {
        cb(false, "找不到 ffmpeg：请把 ffmpeg.exe 放到程序目录 tools/ 下，或在设置里指定路径");
        return;
    }
    QDir().mkpath(QFileInfo(outWav).absolutePath());
    auto *p = new QProcess(this);
    p->setProgram(ffmpeg);
    p->setArguments({ "-y", "-i", mediaPath, "-t", "25", "-vn", "-ar", "16000", "-ac", "1",
                      "-sample_fmt", "s16", outWav });
    connect(p, &QProcess::errorOccurred, this, [p, cb](QProcess::ProcessError) {
        cb(false, "ffmpeg 启动失败");
        p->deleteLater();
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [p, cb, outWav](int code) {
                if (code == 0 && QFile::exists(outWav) && QFileInfo(outWav).size() > 10000)
                    cb(true, QString());
                else
                    cb(false, "ffmpeg 提取音频失败（退出码 " + QString::number(code) + "）");
                p->deleteLater();
            });
    p->start();
}

void VoiceClone::cloneFromFile(const QString &mediaPath, const QString &name, int index)
{
    const QString wav = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QString("/clone-sample-%1.wav").arg(QCoreApplication::applicationPid());
    emit cloneProgress("正在从文件提取声音样本…");
    extractSample(mediaPath, wav, [this, wav, name, index](bool ok, QString err) {
        if (!ok) {
            emit cloneDone(ClonedVoice{}, err);
            return;
        }
        emit cloneProgress("正在上传样本并复刻音色（约 10 秒）…");
        QFile f(wav);
        if (!f.open(QIODevice::ReadOnly)) {
            emit cloneDone(ClonedVoice{}, "读取样本失败");
            return;
        }
        const QByteArray b64 = f.readAll().toBase64();
        f.close();
        QFile::remove(wav);
        QString prefix = name;
        prefix.remove(QRegularExpression("[^0-9a-zA-Z]"));
        if (prefix.isEmpty())
            prefix = QString("voice%1").arg(QDateTime::currentMSecsSinceEpoch() % 100000);
        prefix = prefix.left(10) + QString::number(index);

        QJsonObject input;
        input.insert("action", "create_voice");
        input.insert("target_model", cfg_->ttsCosy.model);
        input.insert("prefix", prefix);
        input.insert("audio", QString("data:audio/wav;base64,") + QString::fromUtf8(b64));
        input.insert("language_hints", QJsonArray{ QJsonValue("zh") });
        QJsonObject root;
        root.insert("model", "voice-enrollment");
        root.insert("input", input);
        const QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
        apiCall(body, [this, name](QJsonObject out, QString err) {
            ClonedVoice v;
            if (err.isEmpty()) {
                v.voiceId = out["voice_id"].toString();
                v.name = name;
                v.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
                if (v.voiceId.isEmpty())
                    err = "未返回 voice_id";
            }
            emit cloneDone(v, err);
        });
    });
}

void VoiceClone::apiCall(const QByteArray &json, std::function<void(QJsonObject, QString)> cb)
{
    const QString domain = cfg_->ttsCosy.domain.isEmpty() ? "dashscope.aliyuncs.com" : cfg_->ttsCosy.domain;
    if (cfg_->ttsCosy.apiKey.isEmpty()) {
        cb(QJsonObject{}, "未配置 CosyVoice 的 DashScope API Key（设置 → 语音合成）");
        return;
    }
    QNetworkRequest req{ QUrl(QString("https://%1/api/v1/services/audio/tts/customization").arg(domain)) };
    req.setRawHeader("Authorization", "Bearer " + cfg_->ttsCosy.apiKey.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = nam_.post(req, json);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            cb(QJsonObject{}, "请求失败: " + reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (root.contains("code") || root.contains("message")) {
            cb(QJsonObject{}, QString("API 错误: %1 %2")
                                   .arg(root["code"].toString(), root["message"].toString()));
            return;
        }
        cb(root["output"].toObject(), QString());
    });
}

void VoiceClone::listVoices()
{
    const QByteArray body = QJsonDocument(QJsonObject{
        { "model", "voice-enrollment" },
        { "input", QJsonObject{ { "action", "list_voice" }, { "page_index", 0 }, { "page_size", 100 } } } })
                                    .toJson(QJsonDocument::Compact);
    apiCall(body, [this](QJsonObject out, QString err) {
        QList<ClonedVoice> voices;
        if (err.isEmpty()) {
            const QJsonArray list = out["voice_list"].toArray();
            for (const auto &v : list) {
                const QJsonObject o = v.toObject();
                ClonedVoice cv;
                cv.voiceId = o["voice_id"].toString();
                cv.createdAt = o["gmt_create"].toString();
                voices.append(cv);
            }
        }
        emit listDone(voices, err);
    });
}

void VoiceClone::deleteVoice(const QString &voiceId)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        { "model", "voice-enrollment" },
        { "input", QJsonObject{ { "action", "delete_voice" }, { "voice_id", voiceId } } } })
                                    .toJson(QJsonDocument::Compact);
    apiCall(body, [this, voiceId](QJsonObject, QString err) { emit deleteDone(voiceId, err); });
}
