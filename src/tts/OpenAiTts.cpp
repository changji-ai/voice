#include "OpenAiTts.h"
#include "AudioDecoder.h"
#include "util/Settings.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

OpenAiTts::OpenAiTts(const AppSettings &cfg) : TtsEngine(nullptr)
{
    baseURL_ = cfg.ttsOpenai.baseURL;
    apiKey_ = cfg.ttsOpenai.apiKey;
    model_ = cfg.ttsOpenai.model;
    voice_ = cfg.ttsOpenai.voice;
    speed_ = cfg.ttsOpenai.speed;
}

void OpenAiTts::speak(const QString &text, int seq)
{
    QString url = baseURL_;
    while (url.endsWith('/'))
        url.chop(1);
    QNetworkRequest req{ QUrl(url + "/audio/speech") };
    req.setRawHeader("Authorization", "Bearer " + apiKey_.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body = QJsonDocument(QJsonObject{
        { "model", model_ },
        { "voice", voice_ },
        { "speed", speed_ },
        { "input", text },
        { "response_format", "mp3" } })
                                  .toJson(QJsonDocument::Compact);

    auto *reply = nam_.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seq] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("TTS 请求失败: " + reply->errorString(), seq);
            return;
        }
        const QByteArray mp3 = reply->readAll();
        if (mp3.size() < 1024) {
            emit errorOccurred(QString("TTS 返回异常: %1").arg(QString::fromUtf8(mp3.left(200))), seq);
            return;
        }
        AudioDecoder::decodeToFloat(
            mp3, "audio/mpeg",
            [this, seq](const QVector<float> &samples, int sr) { emit audioReady(samples, sr, seq); },
            [this, seq](QString err) { emit errorOccurred(err, seq); });
    });
}
