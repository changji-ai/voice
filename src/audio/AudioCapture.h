#pragma once

#include <QObject>
#include <QAudioSource>
#include <QIODevice>
#include <memory>

class Vad;

// 麦克风采集：16k mono s16le；VAD 判句；mute 期间不发数据
class AudioCapture : public QObject
{
    Q_OBJECT
public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    void setVadParams(int silenceMs, double threshold);
    bool start(QString *err = nullptr); // false=没有输入设备
    void stop();
    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

signals:
    void pcmCaptured(const QByteArray &pcm); // s16le 16k mono（muted 时不发）
    void utteranceStarted();
    void utteranceEnded();

private:
    QAudioSource *source_ = nullptr;
    QIODevice *io_ = nullptr;
    std::unique_ptr<Vad> vad_;
    bool muted_ = false;
};
