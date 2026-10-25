#include "AudioCapture.h"
#include "Vad.h"

#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>

AudioCapture::AudioCapture(QObject *parent) : QObject(parent), vad_(std::make_unique<Vad>())
{
    connect(vad_.get(), &Vad::utteranceStarted, this, &AudioCapture::utteranceStarted);
    connect(vad_.get(), &Vad::utteranceEnded, this, &AudioCapture::utteranceEnded);
}

AudioCapture::~AudioCapture()
{
    stop();
}

void AudioCapture::setVadParams(int silenceMs, double threshold)
{
    vad_->setSilenceMs(silenceMs);
    vad_->setThreshold(threshold);
}

bool AudioCapture::start(QString *err)
{
    stop();
    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        if (err)
            *err = "未找到麦克风输入设备";
        return false;
    }
    QAudioFormat fmt;
    fmt.setSampleRate(16000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    source_ = new QAudioSource(dev, fmt, this);
    io_ = source_->start();
    if (!io_) {
        if (err)
            *err = "麦克风启动失败";
        source_->deleteLater();
        source_ = nullptr;
        return false;
    }
    connect(io_, &QIODevice::readyRead, this, [this] {
        const QByteArray pcm = io_->readAll();
        if (pcm.isEmpty())
            return;
        vad_->feed(reinterpret_cast<const int16_t *>(pcm.constData()), pcm.size() / 2);
        if (!muted_)
            emit pcmCaptured(pcm);
    });
    return true;
}

void AudioCapture::stop()
{
    if (source_) {
        source_->stop();
        source_->deleteLater();
        source_ = nullptr;
        io_ = nullptr;
    }
    vad_->reset();
}
