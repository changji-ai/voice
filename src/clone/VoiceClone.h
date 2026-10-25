#pragma once

#include <QObject>
#include <QNetworkAccessManager>

struct AppSettings;
struct ClonedVoice;

// 音色克隆：ffmpeg 提取样本 → 百炼 voice-enrollment 创建复刻音色；查询/删除
class VoiceClone : public QObject
{
    Q_OBJECT
public:
    explicit VoiceClone(QObject *parent = nullptr) : QObject(parent) {}

    static QString findFfmpeg(const AppSettings &cfg);
    void setConfig(const AppSettings *cfg) { cfg_ = cfg; }

    // 提取前 25 秒人声样本（16k mono wav），然后创建克隆音色
    void cloneFromFile(const QString &mediaPath, const QString &name, int index);
    void listVoices();
    void deleteVoice(const QString &voiceId);

signals:
    void cloneProgress(const QString &stage);
    void cloneDone(const ClonedVoice &voice, const QString &err);
    void listDone(const QList<ClonedVoice> &voices, const QString &err);
    void deleteDone(const QString &voiceId, const QString &err);

private:
    void extractSample(const QString &mediaPath, const QString &outWav, std::function<void(bool, QString)> cb);
    void apiCall(const QByteArray &json, std::function<void(QJsonObject, QString)> cb);

    const AppSettings *cfg_ = nullptr;
    QNetworkAccessManager nam_;
};
