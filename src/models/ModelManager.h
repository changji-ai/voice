#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QVector>

// 本地模型管理：ModelScope / hf-mirror 下载 + 就绪判断
struct ModelDef
{
    QString key;   // 唯一键
    QString label; // 显示名
    QString kind;  // "stt" | "tts"
    QString host;  // "modelscope" | "hf"
    QString repo;  // 仓库名
    QStringList files; // 只下载这些文件（空=全部，自动排除 .md 等）
    quint64 approxBytes; // 预估大小（显示用）
};

class ModelManager : public QObject
{
    Q_OBJECT
public:
    static QVector<ModelDef> builtinModels();
    static bool isReady(const ModelDef &def);   // 关键文件都存在
    static ModelDef find(const QString &key);

    explicit ModelManager(QObject *parent = nullptr) : QObject(parent) {}

    void download(const ModelDef &def); // 列文件并逐个下载
    void cancel();

signals:
    void progress(int percent, const QString &file); // 总进度（按字节数）
    void finished(bool ok, const QString &error);

private:
    void downloadNextFile();
    void fetchFileList();

    ModelDef def_;
    struct FileEntry
    {
        QString path;
        qint64 size;
        bool downloaded = false;
    };
    QVector<FileEntry> files_;
    int fileIdx_ = 0;
    qint64 doneBytes_ = 0, totalBytes_ = 0;
    QNetworkAccessManager nam_;
    bool canceled_ = false;
    QString downloadUrl(const QString &path) const;
};
