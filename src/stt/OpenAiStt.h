#pragma once

#include "SttEngine.h"
#include <QNetworkAccessManager>
#include <QByteArray>

class Vad;
struct AppSettings;

// OpenAI 兼容 /audio/transcriptions 分段识别：攒 VAD 段，静音后整段上传
class OpenAiStt : public SttEngine
{
    Q_OBJECT
public:
    explicit OpenAiStt(const AppSettings &cfg);

    QString name() const override { return "OpenAI 兼容分段(云端)"; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void stop() override;

private:
    void submitUtterance();

    QString baseURL_, apiKey_, model_, language_;
    int silenceMs_ = 900;
    bool collecting_ = false; // VAD 说话中
    QByteArray utterance_;    // 攒的 s16le PCM
    Vad *feedVad_ = nullptr;
    QNetworkAccessManager nam_;
    bool busy_ = false;
    QByteArray pending_; // 忙时等上一段返回再发（一般不会发生）
};
