#include "AudioPlayer.h"

#include <QAudioFormat>
#include <QMediaDevices>

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent)
{
    dripTimer_.setInterval(40);
    connect(&dripTimer_, &QTimer::timeout, this, [this] { writeWhatFits(); });
}

AudioPlayer::~AudioPlayer()
{
    clear();
}

void AudioPlayer::setVolume(double v)
{
    volume_ = v;
    if (sink_)
        sink_->setVolume(qBound(0.0, v, 1.0));
}

void AudioPlayer::enqueue(const QVector<float> &samples, int sampleRate, int seq)
{
    waiting_.insert(seq, qMakePair(samples, sampleRate));
    pump();
}

void AudioPlayer::pump()
{
    // 把连续的 seq 转换进 buffer
    while (waiting_.contains(nextSeq_)) {
        const auto [samples, sr] = waiting_.take(nextSeq_);
        nextSeq_++;
        if (sinkRate_ == 0)
            sinkRate_ = sr > 0 ? sr : 24000;
        QByteArray chunk;
        const int16_t dummy = 0;
        Q_UNUSED(dummy);
        if (sr == sinkRate_) {
            chunk.resize(samples.size() * 2);
            auto *out = reinterpret_cast<int16_t *>(chunk.data());
            for (int i = 0; i < samples.size(); i++)
                out[i] = int16_t(qBound(-1.f, samples[i], 1.f) * 32767.f);
        } else {
            // 线性重采样到 sink 采样率
            const double ratio = double(sr) / sinkRate_;
            const int n = qMax(1, int(samples.size() / ratio));
            chunk.resize(n * 2);
            auto *out = reinterpret_cast<int16_t *>(chunk.data());
            for (int i = 0; i < n; i++) {
                const double pos = i * ratio;
                const int i0 = qMin(int(pos), samples.size() - 1);
                const int i1 = qMin(i0 + 1, samples.size() - 1);
                const double frac = pos - i0;
                const float v = samples[i0] * (1 - frac) + samples[i1] * frac;
                out[i] = int16_t(qBound(-1.f, v, 1.f) * 32767.f);
            }
        }
        buffer_.append(chunk);
    }

    if (buffer_.isEmpty())
        return;

    if (!sink_) {
        QAudioFormat fmt;
        fmt.setSampleRate(sinkRate_);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        sink_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
        sink_->setVolume(qBound(0.0, volume_, 1.0));
        io_ = sink_->start();
        connect(sink_, &QAudioSink::stateChanged, this, [this](QAudio::State st) {
            if (st == QAudio::IdleState && buffer_.isEmpty() && waiting_.isEmpty()) {
                dripTimer_.stop();
                playing_ = false;
                sink_->stop();
                sink_->deleteLater();
                sink_ = nullptr;
                io_ = nullptr;
                sinkRate_ = 0;
                emit playbackFinished();
            }
        });
        playing_ = true;
        dripTimer_.start();
        emit playbackStarted();
    }
    writeWhatFits();
}

void AudioPlayer::writeWhatFits()
{
    if (!io_ || !sink_ || buffer_.isEmpty())
        return;
    const int freeBytes = sink_->bytesFree();
    if (freeBytes <= 0)
        return;
    const int n = qMin<qsizetype>(buffer_.size(), freeBytes);
    io_->write(buffer_.constData(), n);
    buffer_.remove(0, n);
}

void AudioPlayer::skipTo(int seq)
{
    // 丢弃所有 <= seq 的待播数据（打断后作废）
    waiting_.clear();
    buffer_.clear();
    nextSeq_ = seq;
}

void AudioPlayer::clear()
{
    dripTimer_.stop();
    waiting_.clear();
    buffer_.clear();
    nextSeq_ = 0;
    if (sink_) {
        sink_->stop();
        sink_->deleteLater();
        sink_ = nullptr;
        io_ = nullptr;
    }
    sinkRate_ = 0;
    playing_ = false;
}
