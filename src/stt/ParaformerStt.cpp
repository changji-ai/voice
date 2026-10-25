#include "ParaformerStt.h"
#include "util/Settings.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QTimer>

ParaformerStt::ParaformerStt(const AppSettings &cfg) : SttEngine(nullptr)
{
    apiKey_ = cfg.sttParaformer.apiKey;
    model_ = cfg.sttParaformer.model;
    domain_ = cfg.sttParaformer.domain.isEmpty() ? "dashscope.aliyuncs.com" : cfg.sttParaformer.domain;
    language_ = cfg.sttParaformer.language;
    connect(&ws_, &QWebSocket::connected, this, [this] { sendRunTask(); });
    connect(&ws_, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        const QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
        const QJsonObject header = doc["header"].toObject();
        const QString event = header["event"].toString();
        if (event == "task-started") {
            started_ = true;
            // 连接建立前攒的音频补发
            if (!preBuffer_.isEmpty()) {
                ws_.sendBinaryMessage(preBuffer_);
                preBuffer_.clear();
            }
            emit engineReady();
        } else if (event == "result-generated") {
            const QJsonObject sentence = doc["payload"].toObject()["output"].toObject()["sentence"].toObject();
            if (sentence["heartbeat"].toBool())
                return;
            const QString text = sentence["text"].toString().trimmed();
            if (sentence["sentence_end"].toBool()) {
                if (!text.isEmpty())
                    emit finalText(text);
            } else if (!text.isEmpty()) {
                emit partial(text);
            }
        } else if (event == "task-finished") {
            started_ = false;
            ws_.close();
        } else if (event == "task-failed") {
            started_ = false;
            emit errorOccurred(QString("Paraformer 识别失败: %1 %2")
                                   .arg(header["error_code"].toString(), header["error_message"].toString()));
            ws_.close();
        }
    });
    connect(&ws_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        emit errorOccurred(QString("Paraformer 连接错误: %1 (%2)").arg(int(e)).arg(ws_.errorString()));
    });
}

ParaformerStt::~ParaformerStt()
{
    stop();
}

void ParaformerStt::start()
{
    QNetworkRequest req{ QUrl(QString("wss://%1/api-ws/v1/inference").arg(domain_)) };
    req.setRawHeader("Authorization", ("Bearer " + apiKey_).toUtf8());
    req.setRawHeader("user-agent", "qt-voice-assistant/1.0");
    ws_.open(req);
}

void ParaformerStt::sendRunTask()
{
    QJsonObject parameters{{ "format", "pcm" },
                           { "sample_rate", 16000 },
                           { "punctuation_prediction_enabled", true },
                           { "disfluency_removal_enabled", false },
                           { "max_sentence_silence", 800 }};
    if (!language_.isEmpty())
        parameters["language_hints"] = QJsonArray{ language_ };
    const QJsonObject msg{
        { "header", QJsonObject{ { "action", "run-task" }, { "task_id", taskId_.toString(QUuid::WithoutBraces) }, { "streaming", "duplex" } } },
        { "payload", QJsonObject{ { "task_group", "audio" },
                                  { "task", "asr" },
                                  { "function", "recognition" },
                                  { "model", model_ },
                                  { "parameters", parameters },
                                  { "input", QJsonObject{} } } }
    };
    ws_.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void ParaformerStt::feed(const QByteArray &pcm)
{
    if (started_ && ws_.state() == QAbstractSocket::ConnectedState)
        ws_.sendBinaryMessage(pcm);
    else if (!stopping_)
        preBuffer_.append(pcm); // 连接建立前先攒着，最多 30 秒
}

void ParaformerStt::stop()
{
    if (ws_.state() == QAbstractSocket::ConnectedState && started_ && !stopping_) {
        stopping_ = true;
        const QJsonObject msg{
            { "header", QJsonObject{ { "action", "finish-task" }, { "task_id", taskId_.toString(QUuid::WithoutBraces) }, { "streaming", "duplex" } } },
            { "payload", QJsonObject{ { "input", QJsonObject{} } } }
        };
        ws_.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
        QTimer::singleShot(3000, &ws_, [this] { ws_.close(); });
    } else {
        ws_.close();
    }
    started_ = false;
}
