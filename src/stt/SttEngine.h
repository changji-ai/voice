#pragma once

#include <QObject>
#include <QString>
#include <memory>

struct AppSettings;

// STT 引擎抽象：feed 输入 16k mono s16le PCM
class SttEngine : public QObject
{
    Q_OBJECT
public:
    explicit SttEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~SttEngine() = default;

    virtual QString name() const = 0;
    virtual void start() = 0;   // 异步/同步启动，成功后 engineReady
    virtual void feed(const QByteArray &pcm) = 0;
    virtual void notifyUtteranceEnd() {} // VAD 判定一句话结束（本地引擎需要；其他引擎忽略）
    virtual void stop() = 0;

    // 工厂：按设置创建。失败（如模型未就绪）返回 nullptr 并给出错误
    static std::unique_ptr<SttEngine> create(const AppSettings &cfg, QString *err);

signals:
    void engineReady();
    void partial(const QString &text);
    void finalText(const QString &text);
    void errorOccurred(const QString &msg);
};
