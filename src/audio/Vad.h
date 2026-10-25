#pragma once

#include <QObject>
#include <QDateTime>

// 能量 VAD：检测说话开始/结束（静音超过 silenceMs 判一句结束）
class Vad : public QObject
{
    Q_OBJECT
public:
    explicit Vad(QObject *parent = nullptr) : QObject(parent) {}

    void setSilenceMs(int ms) { silenceMs_ = ms; }
    void setThreshold(double t) { threshold_ = t; }
    bool inSpeech() const { return inSpeech_; }

    // pcm: 16k mono s16le
    void feed(const int16_t *samples, int n);

    void reset();

signals:
    void utteranceStarted();
    void utteranceEnded();

private:
    double threshold_ = 0.008; // RMS 阈值
    int silenceMs_ = 800;
    bool inSpeech_ = false;
    qint64 speechMs_ = 0;   // 当前这句已说话时长
    qint64 silenceMsRun_ = 0; // 连续静音时长
    qint64 lastTs_ = 0;
};
