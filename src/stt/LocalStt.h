#pragma once

#include "SttEngine.h"

struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;

// 本地实时识别：sherpa-onnx 流式 zipformer + 能量 VAD 断句
class LocalStt : public SttEngine
{
    Q_OBJECT
public:
    explicit LocalStt(const AppSettings &cfg);
    ~LocalStt() override;

    QString name() const override { return "本地流式识别"; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void notifyUtteranceEnd() override;
    void stop() override;

private:
    QString modelDir_;
    const SherpaOnnxOnlineRecognizer *recognizer_ = nullptr;
    const SherpaOnnxOnlineStream *stream_ = nullptr;
    QString lastText_;
};
