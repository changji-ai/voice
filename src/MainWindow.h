#pragma once

#include <QMainWindow>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <vector>
#include <memory>

#include "util/Settings.h"
#include "audio/AudioCapture.h"
#include "audio/AudioPlayer.h"
#include "llm/LlmClient.h"
#include "update/Updater.h"

class SttEngine;
class TtsEngine;
class QProgressDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onToggleSession();
    void onSettings();
    void onInterrupt();
    void onCheckUpdate();

public:
    // 自动化验证用：直接打开设置对话框
    void openSettings() { onSettings(); }

private:
    void appendBubble(const QString &text, bool user);
    QLabel *beginAssistantBubble();
    void updateStatusLine();
    void handleLlmDelta(const QString &delta);
    void flushSentence(bool force);
    void resetAudioPipeline();
    void handleUpdateUi();

    AppSettings cfg_;
    AudioCapture capture_;
    AudioPlayer player_;
    LlmClient llm_;
    std::unique_ptr<SttEngine> stt_;
    std::unique_ptr<TtsEngine> tts_;

    bool session_ = false;
    std::vector<ChatMessage> history_;
    QLabel *subtitle_ = nullptr;      // 实时字幕
    QScrollArea *chatArea_ = nullptr;
    QWidget *chatBox_ = nullptr;
    QVBoxLayout *chatLay_ = nullptr;
    QLabel *currentAssistantLabel_ = nullptr;
    QString assistantFull_;           // 本轮助手完整文本
    QString sentenceBuf_;             // 切句缓冲
    int seqCounter_ = 0;              // TTS 音频序号（单调递增）
    QPushButton *toggleBtn_ = nullptr;
    QPushButton *interruptBtn_ = nullptr;
    QPushButton *settingsBtn_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QLabel *statusLine_ = nullptr;
    Updater *updater_ = nullptr;
    QProgressDialog *updateDlg_ = nullptr;
    bool manualCheck_ = false;
    int round_ = 0;                   // 当前第几轮对话（新的一轮重开 TTS 顺序）
};
