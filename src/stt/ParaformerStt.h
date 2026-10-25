#pragma once

#include "SttEngine.h"
#include <QWebSocket>
#include <QUuid>

// 阿里云百炼 Paraformer 实时识别（WebSocket 流式，服务端断句）
class ParaformerStt : public SttEngine
{
    Q_OBJECT
public:
    explicit ParaformerStt(const AppSettings &cfg);
    ~ParaformerStt() override;

    QString name() const override { return "Paraformer 实时(云端)"; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void stop() override;

private:
    void sendRunTask();

    QString apiKey_, model_, domain_, language_;
    QWebSocket ws_;
    QUuid taskId_ = QUuid::createUuid();
    bool started_ = false;
    bool stopping_ = false;
    QByteArray preBuffer_;
};
