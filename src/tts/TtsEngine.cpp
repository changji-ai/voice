#include "TtsEngine.h"
#include "LocalTts.h"
#include "CosyVoiceTts.h"
#include "OpenAiTts.h"
#include "util/Settings.h"

std::unique_ptr<TtsEngine> TtsEngine::create(const AppSettings &cfg, QString *err)
{
    if (cfg.ttsEngine == "local")
        return std::make_unique<LocalTts>(cfg);
    if (cfg.ttsEngine == "cosyvoice") {
        if (cfg.ttsCosy.apiKey.isEmpty() && err)
            *err = "未配置 CosyVoice 的 DashScope API Key（设置 → 语音合成）";
        return std::make_unique<CosyVoiceTts>(cfg);
    }
    if (cfg.ttsEngine == "openai") {
        if (cfg.ttsOpenai.apiKey.isEmpty() && err)
            *err = "未配置 OpenAI 兼容合成的 API Key（设置 → 语音合成）";
        return std::make_unique<OpenAiTts>(cfg);
    }
    if (err)
        *err = "未知合成引擎: " + cfg.ttsEngine;
    return nullptr;
}
