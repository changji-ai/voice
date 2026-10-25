#include "AudioDecoder.h"

#include <QDir>
#include <QTimer>
#include <QUrl>

AudioDecoder::AudioDecoder(const QByteArray &bytes, const QString &formatHint,
                           std::function<void(QVector<float>, int)> done,
                           std::function<void(QString)> failed)
    : QObject(nullptr), done_(std::move(done)), failed_(std::move(failed))
{
    const QString suffix = formatHint == "audio/mpeg" ? "mp3" : (formatHint == "audio/wav" ? "wav" : "bin");
    file_.setFileTemplate(QDir::tempPath() + "/va-tts-XXXXXX." + suffix);
    file_.open();
    file_.write(bytes);
    file_.close();
}

void AudioDecoder::decodeToFloat(const QByteArray &bytes, const QString &formatHint,
                                 std::function<void(QVector<float>, int)> done,
                                 std::function<void(QString)> failed)
{
    auto *d = new AudioDecoder(bytes, formatHint, std::move(done), std::move(failed));
    d->start();
}

namespace {
void appendBuffer(const QAudioBuffer &buf, QVector<float> *out)
{
    const int channelCount = buf.format().channelCount();
    const int frames = buf.frameCount();
    if (buf.format().sampleFormat() == QAudioFormat::Float) {
        const auto *p = buf.constData<float>();
        for (int i = 0; i < frames; i++) {
            float v = 0;
            for (int c = 0; c < channelCount; c++)
                v += p[i * channelCount + c];
            out->append(v / channelCount);
        }
    } else if (buf.format().sampleFormat() == QAudioFormat::Int16) {
        const auto *p = buf.constData<int16_t>();
        for (int i = 0; i < frames; i++) {
            float v = 0;
            for (int c = 0; c < channelCount; c++)
                v += p[i * channelCount + c];
            out->append(v / channelCount / 32768.f);
        }
    } else if (buf.format().sampleFormat() == QAudioFormat::Int32) {
        const auto *p = buf.constData<int32_t>();
        for (int i = 0; i < frames; i++) {
            float v = 0;
            for (int c = 0; c < channelCount; c++)
                v += p[i * channelCount + c];
            out->append(v / channelCount / 2147483648.f);
        }
    }
}
} // namespace

void AudioDecoder::start()
{
    dec_.setSource(QUrl::fromLocalFile(file_.fileName()));
    sampleRate_ = -1;
    connect(&dec_, &QAudioDecoder::bufferReady, this, [this] {
        while (dec_.bufferAvailable()) {
            const QAudioBuffer buf = dec_.read();
            if (buf.isValid()) {
                if (sampleRate_ < 0)
                    sampleRate_ = buf.format().sampleRate();
                appendBuffer(buf, &samples_);
            }
        }
    });
    connect(&dec_, &QAudioDecoder::finished, this, [this] {
        if (dec_.error() != QAudioDecoder::NoError)
            failed_("解码音频失败: " + dec_.errorString());
        else if (samples_.isEmpty())
            failed_("解码结果为空");
        else
            done_(samples_, sampleRate_);
        deleteLater();
    });
    dec_.start();
}
