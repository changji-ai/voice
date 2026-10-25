#include "CosyVoiceTts.h"
#include "AudioDecoder.h"
#include "util/Settings.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

CosyVoiceTts::CosyVoiceTts(const AppSettings &cfg) : TtsEngine(nullptr)
{
    apiKey_ = cfg.ttsCosy.apiKey;
    model_ = cfg.ttsCosy.model;
    voice_ = cfg.ttsCosy.voice;
    domain_ = cfg.ttsCosy.domain.isEmpty() ? "dashscope.aliyuncs.com" : cfg.ttsCosy.domain;
    rate_ = cfg.ttsCosy.rate;
    pitch_ = cfg.ttsCosy.pitch;
    volume_ = cfg.ttsCosy.volume;
}

void CosyVoiceTts::speak(const QString &text, int seq)
{
    QNetworkRequest req{ QUrl(QString("https://%1/api/v1/services/audio/tts/SpeechSynthesizer").arg(domain_)) };
    req.setRawHeader("Authorization", "Bearer " + apiKey_.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body = QJsonDocument(QJsonObject{
        { "model", model_ },
        { "input", QJsonObject{
                       { "text", text },
                       { "voice", voice_ },
                       { "format", "mp3" },
                       { "sample_rate", 24000 },
                       { "rate", rate_ },
                       { "pitch", pitch_ },
                       { "volume", volume_ } } } })
                                  .toJson(QJsonDocument::Compact);

    auto *reply = nam_.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seq] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("CosyVoice 请求失败: " + reply->errorString(), seq);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QString url = json["output"].toObject()["audio"].toObject()["url"].toString();
        if (url.isEmpty()) {
            emit errorOccurred("CosyVoice 未返回音频 URL", seq);
            return;
        }
        // 下载音频文件
        auto *dl = nam_.get(QNetworkRequest{ QUrl(url) });
        connect(dl, &QNetworkReply::finished, this, [this, dl, seq] {
            dl->deleteLater();
            if (dl->error() != QNetworkReply::NoError) {
                emit errorOccurred("下载合成音频失败: " + dl->errorString(), seq);
                return;
            }
            const QByteArray mp3 = dl->readAll();
            AudioDecoder::decodeToFloat(
                mp3, "audio/mpeg",
                [this, seq](const QVector<float> &samples, int sr) { emit audioReady(samples, sr, seq); },
                [this, seq](QString err) { emit errorOccurred(err, seq); });
        });
    });
}
