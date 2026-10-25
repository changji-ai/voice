// 在线 LLM 验证：用 LlmClient 真实调用一次流式 chat completions
#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
#include <cstdio>
#include "llm/LlmClient.h"
#include "util/Settings.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2) {
        out << "usage: test_llm <api-key> [base-url] [model]\n";
        return 2;
    }
    AppSettings cfg;
    cfg.llmApiKey = argv[1];
    cfg.llmBaseURL = argc > 2 ? argv[2] : "https://dashscope.aliyuncs.com/compatible-mode/v1";
    cfg.llmModel = argc > 3 ? argv[3] : "qwen-turbo";
    cfg.llmSystemPrompt = "你是一个简洁的语音助手，回答控制在两句话内。";

    static std::vector<ChatMessage> history;
    LlmClient llm;
    llm.setConfig(&cfg);
    llm.setHistory(&history);
    QObject::connect(&llm, &LlmClient::deltaText, [&out](const QString &d) {
        out << d;
        out.flush();
    });
    QObject::connect(&llm, &LlmClient::done, [&out, &app](const QString &full) {
        out << "\nDONE, total chars: " << full.size() << "\n";
        app.quit();
    });
    QObject::connect(&llm, &LlmClient::failed, [&out, &app](const QString &e) {
        out << "\nFAILED: " << e << "\n";
        app.exit(1);
    });
    QTimer::singleShot(60000, &app, []() {
        std::puts("TIMEOUT");
        QCoreApplication::exit(3);
    });
    QTimer::singleShot(0, &app, [&llm]() { llm.chat("用一句话介绍杭州。"); });
    return app.exec();
}
