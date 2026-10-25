#pragma once

#include "TtsEngine.h"
#include <QNetworkAccessManager>

// 阿里云百炼 CosyVoice（支持复刻音色）
class CosyVoiceTts : public TtsEngine
{
    Q_OBJECT
public:
    explicit CosyVoiceTts(const AppSettings &cfg);

    QString name() const override { return "CosyVoice(云端)"; }
    void speak(const QString &text, int seq) override;
    void stop() override {}

private:
    QString apiKey_, model_, voice_, domain_;
    double rate_ = 1.0, pitch_ = 1.0;
    int volume_ = 50;
    QNetworkAccessManager nam_;
};
