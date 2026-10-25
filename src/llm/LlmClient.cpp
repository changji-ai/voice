#include "LlmClient.h"
#include "util/Settings.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

void LlmClient::chat(const QString &userText)
{
    cancel();
    full_.clear();
    sseBuf_.clear();
    if (!cfg_) {
        emit failed("LLM 未配置");
        return;
    }

    QJsonArray messages;
    if (!cfg_->llmSystemPrompt.trimmed().isEmpty())
        messages.append(QJsonObject{ { "role", "system" }, { "content", cfg_->llmSystemPrompt } });
    if (history_) {
        // 只带最近 N 轮
        int take = cfg_->llmHistoryTurns * 2;
        int from = static_cast<int>(history_->size()) - take;
        if (from < 0)
            from = 0;
        for (size_t i = from; i < history_->size(); i++) {
            const auto &m = (*history_)[i];
            if (m.role == "system")
                continue;
            messages.append(QJsonObject{ { "role", m.role }, { "content", m.content } });
        }
    }
    messages.append(QJsonObject{ { "role", "user" }, { "content", userText } });

    QString url = cfg_->llmBaseURL;
    while (url.endsWith('/'))
        url.chop(1);
    QNetworkRequest req{ QUrl(url + "/chat/completions") };
    req.setRawHeader("Authorization", "Bearer " + cfg_->llmApiKey.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body = QJsonDocument(QJsonObject{
        { "model", cfg_->llmModel },
        { "messages", messages },
        { "temperature", cfg_->llmTemperature },
        { "stream", true } })
                                  .toJson(QJsonDocument::Compact);

    reply_ = nam_.post(req, body);
    full_.clear();
    sseBuf_.clear();

    connect(reply_, &QNetworkReply::readyRead, this, [this]() {
        sseBuf_ += reply_->readAll();
        int idx;
        while ((idx = sseBuf_.indexOf('\n')) >= 0) {
            const QByteArray line = sseBuf_.left(idx).trimmed();
            sseBuf_.remove(0, idx + 1);
            if (!line.startsWith("data:"))
                continue;
            const QByteArray payload = line.mid(5).trimmed();
            if (payload == "[DONE]")
                continue;
            const QJsonObject json = QJsonDocument::fromJson(payload).object();
            const QString delta = json["choices"].toArray().at(0).toObject()["delta"].toObject()["content"].toString();
            if (!delta.isEmpty()) {
                full_ += delta;
                emit deltaText(delta);
            }
        }
    });

    connect(reply_, &QNetworkReply::finished, this, [this]() {
        if (reply_->error() != QNetworkReply::NoError && full_.isEmpty()) {
            emit failed("LLM 请求失败: " + reply_->errorString()
                        + " HTTP " + QString::number(reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        } else {
            emit done(full_);
        }
        reply_->deleteLater();
        reply_ = nullptr;
    });

    connect(reply_, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (full_.isEmpty())
            emit failed("LLM 网络错误: " + reply_->errorString());
    });
}

void LlmClient::cancel()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
}
