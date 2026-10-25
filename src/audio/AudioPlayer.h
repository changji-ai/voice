#pragma once

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QMap>
#include <QTimer>

// 顺序播放器：按 seq 入队 float32 mono，转 int16 喂给 QAudioSink（限流写入避免阻塞）
class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    void setVolume(double v); // 0..1
    void enqueue(const QVector<float> &samples, int sampleRate, int seq);
    void clear();
    void skipTo(int seq); // 丢弃 seq 之前的队列（打断用）
    bool playing() const { return playing_; }

signals:
    void playbackStarted();
    void playbackFinished(); // 队列全部播完

private:
    void pump();          // 把连续 seq 的数据转 int16 入缓冲，必要时启动 sink
    void writeWhatFits(); // 把缓冲写进 sink（不超过空闲空间）

    QAudioSink *sink_ = nullptr;
    QIODevice *io_ = nullptr;
    int sinkRate_ = 0;
    double volume_ = 0.9;
    int nextSeq_ = 0;
    QMap<int, QPair<QVector<float>, int>> waiting_; // 乱序到达先缓存
    QByteArray buffer_;                             // 待写入的 int16 数据
    QTimer dripTimer_;
    bool playing_ = false;
};
