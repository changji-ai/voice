#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QTemporaryFile>

// 静音压缩音频（mp3）→ 解码为 float32 mono 采样
class AudioDecoder : public QObject
{
    Q_OBJECT
public:
    static void decodeToFloat(const QByteArray &bytes, const QString &formatHint,
                              std::function<void(QVector<float>, int)> done,
                              std::function<void(QString)> failed);

private:
    explicit AudioDecoder(const QByteArray &bytes, const QString &formatHint,
                          std::function<void(QVector<float>, int)> done,
                          std::function<void(QString)> failed);
    void start();

    QTemporaryFile file_;
    QAudioDecoder dec_;
    QVector<float> samples_;
    int sampleRate_ = 24000;
    std::function<void(QVector<float>, int)> done_;
    std::function<void(QString)> failed_;
};
