#pragma once

#include "TtsEngine.h"
#include <QMutex>
#include <QQueue>
#include <QPair>

struct SherpaOnnxOfflineTts;

// 本地 TTS：sherpa-onnx kokoro，合成在工作线程，按 seq 顺序回调
class LocalTts : public TtsEngine
{
    Q_OBJECT
public:
    explicit LocalTts(const AppSettings &cfg);
    ~LocalTts() override;

    QString name() const override { return "本地合成(kokoro)"; }
    void speak(const QString &text, int seq) override;
    void stop() override;

    // 模型是否就绪（工厂创建前检查）
    static bool modelReady(const AppSettings &cfg);

private:
    void synthesizeNext();

    const SherpaOnnxOfflineTts *tts_ = nullptr;
    int sid_ = 0;
    double speed_ = 1.0;
    QMutex mutex_;
    QQueue<QPair<QString, int>> queue_;
    bool running_ = false;
    bool stopped_ = false;
};
