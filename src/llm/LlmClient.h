#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <vector>

struct AppSettings;

struct ChatMessage
{
    QString role; // system / user / assistant
    QString content;
};

// OpenAI 兼容 chat completions 流式客户端
class LlmClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr) : QObject(parent) {}

    void setConfig(const AppSettings *cfg) { cfg_ = cfg; }
    void setHistory(const std::vector<ChatMessage> *h) { history_ = h; }

    // 流式对话：deltaText 增量，done 完成并给全文
    void chat(const QString &userText);
    void cancel();

signals:
    void deltaText(const QString &delta);
    void done(const QString &fullText);
    void failed(const QString &error);

private:
    const AppSettings *cfg_ = nullptr;
    const std::vector<ChatMessage> *history_ = nullptr;
    QNetworkAccessManager nam_;
    class QNetworkReply *reply_ = nullptr;
    QString full_;
    QByteArray sseBuf_;
};
