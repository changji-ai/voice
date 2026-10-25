#include "OpenAiStt.h"
#include "util/Settings.h"
#include "audio/Vad.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QTimer>

namespace {
// s16le PCM 前加 44 字节 WAV 头
QByteArray toWav(const QByteArray &pcm, int sampleRate = 16000)
{
    const int32_t dataLen = pcm.size();
    QByteArray wav;
    wav.reserve(44 + dataLen);
    auto le16 = [&](int16_t v) { char b[2]{ char(v & 0xff), char((v >> 8) & 0xff) }; wav.append(b, 2); };
    auto le32 = [&](int32_t v) { char b[4]{ char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff) }; wav.append(b, 4); };
    wav.append("RIFF"); le32(36 + dataLen); wav.append("WAVEfmt "); le32(16);
    le16(1); le16(1); le32(sampleRate); le32(sampleRate * 2); le16(2); le16(16);
    wav.append("data"); le32(dataLen); wav.append(pcm);
    return wav;
}
} // namespace

OpenAiStt::OpenAiStt(const AppSettings &cfg) : SttEngine(nullptr)
{
    baseURL_ = cfg.sttOpenai.baseURL;
    apiKey_ = cfg.sttOpenai.apiKey;
    model_ = cfg.sttOpenai.model;
    language_ = cfg.sttOpenai.language;
    silenceMs_ = cfg.sttOpenai.silenceMs;
    // 用一个内部 VAD 决定段的起止（和本地引擎共用阈值设置）
    auto *vad = new Vad(this);
    vad->setSilenceMs(silenceMs_);
    vad->setThreshold(cfg.sttLocal.vadThreshold);
    connect(vad, &Vad::utteranceStarted, this, [this] {
        collecting_ = true;
        utterance_.clear();
    });
    connect(vad, &Vad::utteranceEnded, this, [this] {
        collecting_ = false;
        // 不足 0.4s 的片段丢弃
        if (utterance_.size() >= 2 * 16000 * 0.4)
            submitUtterance();
        utterance_.clear();
    });
    feedVad_ = vad;
}

void OpenAiStt::start()
{
    emit engineReady(); // HTTP 引擎无需预连接
}

void OpenAiStt::feed(const QByteArray &pcm)
{
    feedVad_->feed(reinterpret_cast<const int16_t *>(pcm.constData()), pcm.size() / 2);
    if (collecting_)
        utterance_.append(pcm);
}

void OpenAiStt::submitUtterance()
{
    if (busy_) {
        pending_ = utterance_; // 保存最近一段
        return;
    }
    busy_ = true;
    const QByteArray wav = toWav(utterance_);

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "audio/wav");
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"file\"; filename=\"audio.wav\"");
    filePart.setBody(wav);
    multi->append(filePart);
    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"model\"");
    modelPart.setBody(model_.toUtf8());
    multi->append(modelPart);
    if (!language_.isEmpty()) {
        QHttpPart langPart;
        langPart.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"language\"");
        langPart.setBody(language_.toUtf8());
        multi->append(langPart);
    }

    QString url = baseURL_;
    while (url.endsWith('/'))
        url.chop(1);
    QNetworkRequest req{ QUrl(url + "/audio/transcriptions") };
    req.setRawHeader("Authorization", "Bearer " + apiKey_.toUtf8());
    auto *reply = nam_.post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        busy_ = false;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("识别请求失败: " + reply->errorString());
        } else {
            const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
            const QString text = json["text"].toString().trimmed();
            if (!text.isEmpty())
                emit finalText(text);
        }
        if (!pending_.isEmpty() && pending_.size() >= 2 * 16000 * 0.4) {
            utterance_ = pending_;
            pending_.clear();
            QTimer::singleShot(0, this, [this] { submitUtterance(); });
        } else {
            pending_.clear();
        }
    });
}

void OpenAiStt::stop()
{
    if (collecting_ && utterance_.size() >= 2 * 16000 * 0.4)
        submitUtterance();
    collecting_ = false;
    utterance_.clear();
}
