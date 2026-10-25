#pragma once

#include <QObject>
#include <QVector>
#include <memory>

struct AppSettings;

// TTS 引擎抽象：speak(text, seq) 异步合成，完成后按 seq 顺序回调 audioReady
class TtsEngine : public QObject
{
    Q_OBJECT
public:
    explicit TtsEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~TtsEngine() = default;

    virtual QString name() const = 0;
    virtual void speak(const QString &text, int seq) = 0;
    virtual void stop() = 0; // 停止当前及排队的合成

    // 工厂。err 给出失败原因（如模型未就绪/未配置 Key）
    static std::unique_ptr<TtsEngine> create(const AppSettings &cfg, QString *err);

signals:
    void audioReady(const QVector<float> &samples, int sampleRate, int seq);
    void errorOccurred(const QString &msg, int seq);
};
