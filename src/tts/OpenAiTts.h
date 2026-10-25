#pragma once

#include "TtsEngine.h"
#include <QNetworkAccessManager>

// OpenAI 兼容 /audio/speech
class OpenAiTts : public TtsEngine
{
    Q_OBJECT
public:
    explicit OpenAiTts(const AppSettings &cfg);

    QString name() const override { return "OpenAI 兼容(云端)"; }
    void speak(const QString &text, int seq) override;
    void stop() override {}

private:
    QString baseURL_, apiKey_, model_, voice_;
    double speed_ = 1.0;
    QNetworkAccessManager nam_;
};
