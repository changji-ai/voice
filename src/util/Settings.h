#pragma once

#include <QString>
#include <QList>

struct SttLocalConf {
    QString modelKey = "zipformer-zh-en";
    int silenceMs = 800;
    double vadThreshold = 0.008; // RMS 阈值（灵敏度）
};
struct SttParaformerConf {
    QString apiKey;
    QString model = "paraformer-realtime-v2";
    QString domain = "dashscope.aliyuncs.com";
    QString language; // 空=自动
};
struct SttOpenaiConf {
    QString baseURL = "https://api.openai.com/v1";
    QString apiKey;
    QString model = "whisper-1";
    QString language;
    int silenceMs = 900;
};
struct TtsLocalConf {
    QString modelKey = "kokoro-multi-lang";
    int sid = 45; // kokoro 音色编号（45=zf_xiaobei 中文女声）
    double speed = 1.0;
};
struct TtsCosyConf {
    QString apiKey;
    QString model = "cosyvoice-v2";
    QString voice = "longxiaochun_v2";
    double rate = 1.0;   // 0.5-2
    double pitch = 1.0;  // 0.5-2
    int volume = 50;     // 0-100
    QString domain = "dashscope.aliyuncs.com";
};
struct TtsOpenaiConf {
    QString baseURL = "https://api.openai.com/v1";
    QString apiKey;
    QString model = "gpt-4o-mini-tts";
    QString voice = "alloy";
    double speed = 1.0;
};
struct ClonedVoice {
    QString voiceId;
    QString name;
    QString createdAt;
};

struct AppSettings {
    // LLM
    QString llmBaseURL = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    QString llmApiKey;
    QString llmModel = "qwen-plus";
    QString llmSystemPrompt =
        "你是一个友好的实时语音助手。回答要口语化、简短自然，一般不超过两三句话，适合直接读出"
        "来。不要使用 markdown、列表和表情。";
    double llmTemperature = 0.7;
    int llmHistoryTurns = 6;

    // STT
    QString sttEngine = "local"; // local | paraformer | openai
    SttLocalConf sttLocal;
    SttParaformerConf sttParaformer;
    SttOpenaiConf sttOpenai;

    // TTS
    QString ttsEngine = "local"; // local | cosyvoice | openai
    TtsLocalConf ttsLocal;
    TtsCosyConf ttsCosy;
    TtsOpenaiConf ttsOpenai;

    // 通用
    double volume = 0.9; // 0-1
    bool pauseSttWhileSpeaking = true;
    QString ffmpegPath; // 音色克隆提取音频用；空=自动找 exe 旁 tools/ffmpeg.exe 或 PATH

    QList<ClonedVoice> clonedVoices;

    void load();
    void save() const;
};

QString modelsRoot(); // 本地模型存放目录
