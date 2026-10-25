#include "SttEngine.h"
#include "LocalStt.h"
#include "ParaformerStt.h"
#include "OpenAiStt.h"
#include "util/Settings.h"

std::unique_ptr<SttEngine> SttEngine::create(const AppSettings &cfg, QString *err)
{
    if (cfg.sttEngine == "local")
        return std::make_unique<LocalStt>(cfg);
    if (cfg.sttEngine == "paraformer") {
        if (cfg.sttParaformer.apiKey.isEmpty() && err)
            *err = "未配置 Paraformer 的 DashScope API Key（设置 → 语音识别）";
        return std::make_unique<ParaformerStt>(cfg);
    }
    if (cfg.sttEngine == "openai") {
        if (cfg.sttOpenai.apiKey.isEmpty() && err)
            *err = "未配置 OpenAI 兼容识别的 API Key（设置 → 语音识别）";
        return std::make_unique<OpenAiStt>(cfg);
    }
    if (err)
        *err = "未知识别引擎: " + cfg.sttEngine;
    return nullptr;
}
