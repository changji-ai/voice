#include "Settings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

QString modelsRoot()
{
    // 统一：C:/Users/<user>/AppData/Roaming/VoiceAssistant/models
    const QString base = qEnvironmentVariable("APPDATA");
    if (!base.isEmpty())
        return base + "/VoiceAssistant/models";
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
}

void AppSettings::load()
{
    QSettings s;
    llmBaseURL = s.value("llm/baseURL", llmBaseURL).toString();
    llmApiKey = s.value("llm/apiKey").toString();
    llmModel = s.value("llm/model", llmModel).toString();
    llmSystemPrompt = s.value("llm/systemPrompt", llmSystemPrompt).toString();
    llmTemperature = s.value("llm/temperature", llmTemperature).toDouble();
    llmHistoryTurns = s.value("llm/historyTurns", llmHistoryTurns).toInt();

    sttEngine = s.value("stt/engine", sttEngine).toString();
    sttLocal.modelKey = s.value("stt/local/modelKey", sttLocal.modelKey).toString();
    sttLocal.silenceMs = s.value("stt/local/silenceMs", sttLocal.silenceMs).toInt();
    sttLocal.vadThreshold = s.value("stt/local/vadThreshold", sttLocal.vadThreshold).toDouble();
    sttParaformer.apiKey = s.value("stt/paraformer/apiKey").toString();
    sttParaformer.model = s.value("stt/paraformer/model", sttParaformer.model).toString();
    sttParaformer.domain = s.value("stt/paraformer/domain", sttParaformer.domain).toString();
    sttParaformer.language = s.value("stt/paraformer/language").toString();
    sttOpenai.baseURL = s.value("stt/openai/baseURL", sttOpenai.baseURL).toString();
    sttOpenai.apiKey = s.value("stt/openai/apiKey").toString();
    sttOpenai.model = s.value("stt/openai/model", sttOpenai.model).toString();
    sttOpenai.language = s.value("stt/openai/language").toString();
    sttOpenai.silenceMs = s.value("stt/openai/silenceMs", sttOpenai.silenceMs).toInt();

    ttsEngine = s.value("tts/engine", ttsEngine).toString();
    ttsLocal.modelKey = s.value("tts/local/modelKey", ttsLocal.modelKey).toString();
    ttsLocal.sid = s.value("tts/local/sid", ttsLocal.sid).toInt();
    ttsLocal.speed = s.value("tts/local/speed", ttsLocal.speed).toDouble();
    ttsCosy.apiKey = s.value("tts/cosyvoice/apiKey").toString();
    ttsCosy.model = s.value("tts/cosyvoice/model", ttsCosy.model).toString();
    ttsCosy.voice = s.value("tts/cosyvoice/voice", ttsCosy.voice).toString();
    ttsCosy.rate = s.value("tts/cosyvoice/rate", ttsCosy.rate).toDouble();
    ttsCosy.pitch = s.value("tts/cosyvoice/pitch", ttsCosy.pitch).toDouble();
    ttsCosy.volume = s.value("tts/cosyvoice/volume", ttsCosy.volume).toInt();
    ttsCosy.domain = s.value("tts/cosyvoice/domain", ttsCosy.domain).toString();
    ttsOpenai.baseURL = s.value("tts/openai/baseURL", ttsOpenai.baseURL).toString();
    ttsOpenai.apiKey = s.value("tts/openai/apiKey").toString();
    ttsOpenai.model = s.value("tts/openai/model", ttsOpenai.model).toString();
    ttsOpenai.voice = s.value("tts/openai/voice", ttsOpenai.voice).toString();
    ttsOpenai.speed = s.value("tts/openai/speed", ttsOpenai.speed).toDouble();

    volume = s.value("common/volume", volume).toDouble();
    pauseSttWhileSpeaking = s.value("common/pauseSttWhileSpeaking", pauseSttWhileSpeaking).toBool();
    ffmpegPath = s.value("common/ffmpegPath").toString();

    clonedVoices.clear();
    const QJsonDocument doc = QJsonDocument::fromJson(s.value("common/clonedVoices").toByteArray());
    if (doc.isArray()) {
        for (const auto &v : doc.array()) {
            const QJsonObject o = v.toObject();
            clonedVoices.append({ o["voiceId"].toString(), o["name"].toString(),
                                  o["createdAt"].toString() });
        }
    }
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue("llm/baseURL", llmBaseURL);
    s.setValue("llm/apiKey", llmApiKey);
    s.setValue("llm/model", llmModel);
    s.setValue("llm/systemPrompt", llmSystemPrompt);
    s.setValue("llm/temperature", llmTemperature);
    s.setValue("llm/historyTurns", llmHistoryTurns);

    s.setValue("stt/engine", sttEngine);
    s.setValue("stt/local/modelKey", sttLocal.modelKey);
    s.setValue("stt/local/silenceMs", sttLocal.silenceMs);
    s.setValue("stt/local/vadThreshold", sttLocal.vadThreshold);
    s.setValue("stt/paraformer/apiKey", sttParaformer.apiKey);
    s.setValue("stt/paraformer/model", sttParaformer.model);
    s.setValue("stt/paraformer/domain", sttParaformer.domain);
    s.setValue("stt/paraformer/language", sttParaformer.language);
    s.setValue("stt/openai/baseURL", sttOpenai.baseURL);
    s.setValue("stt/openai/apiKey", sttOpenai.apiKey);
    s.setValue("stt/openai/model", sttOpenai.model);
    s.setValue("stt/openai/language", sttOpenai.language);
    s.setValue("stt/openai/silenceMs", sttOpenai.silenceMs);

    s.setValue("tts/engine", ttsEngine);
    s.setValue("tts/local/modelKey", ttsLocal.modelKey);
    s.setValue("tts/local/sid", ttsLocal.sid);
    s.setValue("tts/local/speed", ttsLocal.speed);
    s.setValue("tts/cosyvoice/apiKey", ttsCosy.apiKey);
    s.setValue("tts/cosyvoice/model", ttsCosy.model);
    s.setValue("tts/cosyvoice/voice", ttsCosy.voice);
    s.setValue("tts/cosyvoice/rate", ttsCosy.rate);
    s.setValue("tts/cosyvoice/pitch", ttsCosy.pitch);
    s.setValue("tts/cosyvoice/volume", ttsCosy.volume);
    s.setValue("tts/cosyvoice/domain", ttsCosy.domain);
    s.setValue("tts/openai/baseURL", ttsOpenai.baseURL);
    s.setValue("tts/openai/apiKey", ttsOpenai.apiKey);
    s.setValue("tts/openai/model", ttsOpenai.model);
    s.setValue("tts/openai/voice", ttsOpenai.voice);
    s.setValue("tts/openai/speed", ttsOpenai.speed);

    s.setValue("common/volume", volume);
    s.setValue("common/pauseSttWhileSpeaking", pauseSttWhileSpeaking);
    s.setValue("common/ffmpegPath", ffmpegPath);

    QJsonArray arr;
    for (const auto &v : clonedVoices) {
        QJsonObject o;
        o["voiceId"] = v.voiceId;
        o["name"] = v.name;
        o["createdAt"] = v.createdAt;
        arr.append(o);
    }
    s.setValue("common/clonedVoices", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
