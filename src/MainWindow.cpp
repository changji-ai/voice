#include "MainWindow.h"
#include "SettingsDialog.h"
#include "stt/SttEngine.h"
#include "tts/TtsEngine.h"
#include "tts/LocalTts.h"
#include "models/ModelManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QDateTime>
#include <QRegularExpression>
#include <QScrollBar>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <limits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    cfg_.load();
    llm_.setConfig(&cfg_);
    llm_.setHistory(&history_);

    setWindowTitle(QString("实时语音助手 v%1").arg(Updater::currentVersion()));
    resize(640, 720);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(8);

    // 顶部状态
    auto *top = new QHBoxLayout;
    statusLine_ = new QLabel(central);
    auto *updateBtn = new QPushButton("⟳ 检查更新", central);
    settingsBtn_ = new QPushButton("⚙ 设置", central);
    top->addWidget(statusLine_, 1);
    top->addWidget(updateBtn);
    top->addWidget(settingsBtn_);
    root->addLayout(top);

    // 消息区
    chatArea_ = new QScrollArea(central);
    chatArea_->setWidgetResizable(true);
    chatArea_->setFrameShape(QFrame::NoFrame);
    chatBox_ = new QWidget(chatArea_);
    chatLay_ = new QVBoxLayout(chatBox_);
    chatLay_->setAlignment(Qt::AlignTop);
    chatLay_->setSpacing(10);
    chatLay_->addStretch(1);
    chatArea_->setWidget(chatBox_);
    root->addWidget(chatArea_, 1);

    // 实时字幕
    subtitle_ = new QLabel(" ", central);
    subtitle_->setStyleSheet("color:#1a73e8; font-size:15px;");
    subtitle_->setWordWrap(true);
    subtitle_->setMinimumHeight(28);
    root->addWidget(subtitle_);

    // 底部控制
    auto *bottom = new QHBoxLayout;
    toggleBtn_ = new QPushButton("🎤  开始对话", central);
    toggleBtn_->setMinimumHeight(56);
    toggleBtn_->setStyleSheet("QPushButton { font-size: 18px; font-weight: bold; }");
    interruptBtn_ = new QPushButton("✋ 打断", central);
    interruptBtn_->setMinimumHeight(56);
    interruptBtn_->setEnabled(false);
    volumeSlider_ = new QSlider(Qt::Vertical, central);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(int(cfg_.volume * 100));
    volumeSlider_->setFixedHeight(56);
    bottom->addWidget(toggleBtn_, 1);
    bottom->addWidget(interruptBtn_);
    bottom->addWidget(new QLabel("🔊", central));
    bottom->addWidget(volumeSlider_);
    root->addLayout(bottom);
    setCentralWidget(central);

    player_.setVolume(cfg_.volume);
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int v) {
        cfg_.volume = v / 100.0;
        player_.setVolume(cfg_.volume);
        cfg_.save();
    });

    connect(settingsBtn_, &QPushButton::clicked, this, &MainWindow::onSettings);
    connect(toggleBtn_, &QPushButton::clicked, this, &MainWindow::onToggleSession);
    connect(interruptBtn_, &QPushButton::clicked, this, &MainWindow::onInterrupt);
    connect(updateBtn, &QPushButton::clicked, this, &MainWindow::onCheckUpdate);

    // 自动更新：启动 5 秒后静默检查一次
    updater_ = new Updater(this);
    connect(updater_, &Updater::upToDate, this, [this](const QString &v) {
        if (manualCheck_)
            QMessageBox::information(this, "检查更新", "已是最新版本（v" + v + "）");
    });
    connect(updater_, &Updater::checkFailed, this, [this](const QString &e) {
        if (manualCheck_)
            QMessageBox::warning(this, "检查更新", "检查失败：" + e);
    });
    connect(updater_, &Updater::updateAvailable, this, [this] { handleUpdateUi(); });
    connect(updater_, &Updater::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (updateDlg_) {
            updateDlg_->setMaximum(int(qMin<qint64>(total, INT_MAX)));
            updateDlg_->setValue(int(qMin<qint64>(done, INT_MAX)));
        }
    });
    connect(updater_, &Updater::downloadFailed, this, [this](const QString &e) {
        if (updateDlg_) {
            updateDlg_->close();
            updateDlg_->deleteLater();
            updateDlg_ = nullptr;
        }
        QMessageBox::warning(this, "更新", "下载失败：" + e);
    });
    connect(updater_, &Updater::readyToRestart, this, [this] {
        if (updateDlg_) {
            updateDlg_->close();
            updateDlg_->deleteLater();
            updateDlg_ = nullptr;
        }
        QString err;
        if (updater_->restartNow(&err)) {
            // 静默安装：退出当前进程，脚本覆盖文件后自动启动新版
            if (session_)
                onToggleSession();
            QApplication::quit();
        } else {
            QMessageBox::warning(this, "更新", "启动安装失败：" + err);
        }
    });
    QTimer::singleShot(5000, updater_, &Updater::checkNow);

    // 音频采集 → STT
    connect(&capture_, &AudioCapture::pcmCaptured, this, [this](const QByteArray &pcm) {
        if (stt_)
            stt_->feed(pcm);
    });
    connect(&capture_, &AudioCapture::utteranceEnded, this, [this] {
        if (stt_)
            stt_->notifyUtteranceEnd();
    });

    // 播放暂停收音
    connect(&player_, &AudioPlayer::playbackStarted, this, [this] {
        if (cfg_.pauseSttWhileSpeaking && session_)
            capture_.setMuted(true);
    });
    connect(&player_, &AudioPlayer::playbackFinished, this, [this] {
        capture_.setMuted(false);
    });

    // LLM → 流式更新 + 切句 TTS
    connect(&llm_, &LlmClient::deltaText, this, &MainWindow::handleLlmDelta);
    connect(&llm_, &LlmClient::done, this, [this](const QString &) {
        flushSentence(true);
        if (!assistantFull_.isEmpty())
            history_.push_back({ "assistant", assistantFull_ });
        currentAssistantLabel_ = nullptr;
    });
    connect(&llm_, &LlmClient::failed, this, [this](const QString &err) {
        appendBubble("⚠ " + err, false);
        currentAssistantLabel_ = nullptr;
    });

    updateStatusLine();
}

MainWindow::~MainWindow() = default;

void MainWindow::updateStatusLine()
{
    if (!session_) {
        QString sttName = cfg_.sttEngine == "local" ? "本地识别"
                          : cfg_.sttEngine == "paraformer" ? "Paraformer"
                                                           : "OpenAI分段";
        QString ttsName = cfg_.ttsEngine == "local" ? "本地合成(kokoro)"
                          : cfg_.ttsEngine == "cosyvoice" ? "CosyVoice"
                                                          : "OpenAI兼容";
        statusLine_->setText(QString("链路：%1 → %2 → %3").arg(sttName, cfg_.llmModel, ttsName));
        return;
    }
    statusLine_->setText("正在对话（说话后自动断句，回答自动朗读；✋ 可打断）");
}

void MainWindow::appendBubble(const QString &text, bool user)
{
    auto *frame = new QFrame(chatBox_);
    frame->setStyleSheet(user
                             ? "QFrame { background:#1a73e8; border-radius:10px; } QLabel { color:white; }"
                             : "QFrame { background:#f1f3f4; border-radius:10px; }");
    auto *lay = new QVBoxLayout(frame);
    lay->setContentsMargins(12, 8, 12, 8);
    auto *label = new QLabel(text, frame);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(label);
    auto *row = new QWidget(chatBox_);
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);
    if (user) {
        rowLay->addStretch(1);
        rowLay->addWidget(frame, 4);
    } else {
        rowLay->addWidget(frame, 4);
        rowLay->addStretch(1);
    }
    chatLay_->insertWidget(chatLay_->count() - 1, row);
    QTimer::singleShot(0, this, [this] {
        QScrollBar *sb = chatArea_->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

QLabel *MainWindow::beginAssistantBubble()
{
    appendBubble("…", false);
    // 取最后一个气泡里的 QLabel
    const int idx = chatLay_->count() - 2; // 倒数第二个是 row（最后是 stretch）
    if (idx >= 0) {
        if (auto *row = qobject_cast<QWidget *>(chatLay_->itemAt(idx)->widget())) {
            const auto labels = row->findChildren<QLabel *>();
            if (!labels.isEmpty())
                return labels.first();
        }
    }
    return nullptr;
}

void MainWindow::onToggleSession()
{
    if (!session_) {
        cfg_.load();
        llm_.setConfig(&cfg_);
        round_++;

        // 检查 LLM
        if (cfg_.llmApiKey.isEmpty()) {
            appendBubble("⚠ 请先在设置 → 对话模型里填写 API Key", false);
            return;
        }
        // 创建 TTS
        QString ttsErr;
        if (cfg_.ttsEngine == "local" && !LocalTts::modelReady(cfg_)) {
            ttsErr = "本地 TTS 模型未下载（设置 → 语音合成 → 下载模型）";
        }
        if (ttsErr.isEmpty())
            tts_ = TtsEngine::create(cfg_, &ttsErr);
        if (!tts_) {
            appendBubble("⚠ " + ttsErr, false);
            return;
        }
        // 创建 STT
        QString sttErr;
        if (cfg_.sttEngine == "local") {
            const ModelDef def = ModelManager::find(cfg_.sttLocal.modelKey);
            if (!ModelManager::isReady(def))
                sttErr = "本地识别模型未下载（设置 → 语音识别 → 下载模型）";
        }
        if (sttErr.isEmpty())
            stt_ = SttEngine::create(cfg_, &sttErr);
        if (!stt_) {
            appendBubble("⚠ " + sttErr, false);
            tts_.reset();
            return;
        }

        connect(stt_.get(), &SttEngine::partial, this, [this](const QString &text) {
            subtitle_->setText("🎙 " + text);
        });
        connect(stt_.get(), &SttEngine::finalText, this, [this](const QString &text) {
            subtitle_->setText(" ");
            appendBubble(text, true);
            history_.push_back({ "user", text });
            round_++;
            assistantFull_.clear();
            sentenceBuf_.clear();
            currentAssistantLabel_ = beginAssistantBubble();
            if (currentAssistantLabel_)
                currentAssistantLabel_->setText("…");
            llm_.chat(text);
        });
        connect(stt_.get(), &SttEngine::errorOccurred, this, [this](const QString &msg) {
            appendBubble("⚠ 识别：" + msg, false);
        });
        connect(tts_.get(), &TtsEngine::audioReady, this,
                [this](const QVector<float> &samples, int sr, int seq) {
                    player_.enqueue(samples, sr, seq);
                });
        connect(tts_.get(), &TtsEngine::errorOccurred, this,
                [this](const QString &msg, int) { appendBubble("⚠ 合成：" + msg, false); });

        // 采集参数 & 启动
        const int silenceMs = cfg_.sttEngine == "openai" ? cfg_.sttOpenai.silenceMs
                                  : cfg_.sttEngine == "local" ? cfg_.sttLocal.silenceMs
                                                               : cfg_.sttLocal.silenceMs;
        capture_.setVadParams(silenceMs, cfg_.sttLocal.vadThreshold);
        player_.setVolume(cfg_.volume);
        session_ = true;
        toggleBtn_->setText("■  结束对话");
        interruptBtn_->setEnabled(true);
        updateStatusLine();

        stt_->start(); // engineReady 后开始收音（本地加载模型要几秒）
        if (cfg_.sttEngine != "local") {
            QString err;
            capture_.start(&err);
            if (!err.isEmpty())
                appendBubble("⚠ " + err, false);
        } else {
            // 本地引擎 ready 需要加载模型
            connect(stt_.get(), &SttEngine::engineReady, this, [this] {
                QString err;
                capture_.start(&err);
                if (!err.isEmpty())
                    appendBubble("⚠ " + err, false);
                else
                    appendBubble("本地识别已就绪，开始说话吧。", false);
            }, Qt::SingleShotConnection);
        }
    } else {
        // 停止
        session_ = false;
        capture_.stop();
        player_.clear();
        llm_.cancel();
        if (stt_)
            stt_->stop();
        stt_.reset();
        tts_.reset();
        capture_.setMuted(false);
        subtitle_->setText(" ");
        toggleBtn_->setText("🎤  开始对话");
        interruptBtn_->setEnabled(false);
        updateStatusLine();
    }
}

void MainWindow::onInterrupt()
{
    llm_.cancel();
    flushSentence(false);
    sentenceBuf_.clear();
    player_.clear();
    player_.skipTo(++seqCounter_);
    capture_.setMuted(false);
}

void MainWindow::onCheckUpdate()
{
    manualCheck_ = true;
    updater_->checkNow();
}

void MainWindow::handleUpdateUi()
{
    const QString cur = Updater::currentVersion();
    const QString next = updater_->latestVersion();
    QString notes = updater_->latestNotes();
    notes.truncate(400);
    const double mb = updater_->latestSize() / 1048576.0;
    const QString msg = QString("发现新版本 v%1（当前 v%2）\n\n下载大小：%3MB\n\n%4\n\n下载并静默安装？")
                            .arg(next, cur)
                            .arg(mb, 0, 'f', 1)
                            .arg(notes);
    if (QMessageBox::question(this, "发现新版本", msg) != QMessageBox::Yes) {
        manualCheck_ = false;
        return;
    }
    updateDlg_ = new QProgressDialog("正在下载 v" + next + " …", "取消", 0, 100, this);
    updateDlg_->setWindowModality(Qt::WindowModal);
    updateDlg_->setMinimumDuration(0);
    updateDlg_->show();
    connect(updateDlg_, &QProgressDialog::canceled, this, [this] {
        // 简单起见：取消只关对话框，下载继续（完成后仍会提示重启）
        updateDlg_->deleteLater();
        updateDlg_ = nullptr;
    });
    updater_->downloadAndInstall(updater_->latestUrl());
    manualCheck_ = false;
}

void MainWindow::onSettings()
{
    const bool wasSession = session_;
    if (wasSession)
        onToggleSession(); // 先停会话再改设置
    SettingsDialog dlg(&cfg_, this);
    extern int g_openTab;
    if (g_openTab >= 0)
        dlg.selectTab(g_openTab);
    dlg.exec();
    cfg_.load();
    llm_.setConfig(&cfg_);
    player_.setVolume(cfg_.volume);
    volumeSlider_->setValue(int(cfg_.volume * 100));
    updateStatusLine();
}

void MainWindow::handleLlmDelta(const QString &delta)
{
    assistantFull_ += delta;
    sentenceBuf_ += delta;
    if (currentAssistantLabel_)
        currentAssistantLabel_->setText(assistantFull_);
    // 遇到句末标点切句送 TTS
    static const QRegularExpression sep("[。！？!?；;\\n]");
    qsizetype idx;
    while ((idx = sentenceBuf_.lastIndexOf(sep)) >= 0) {
        const QString sentence = sentenceBuf_.left(idx + 1).trimmed();
        sentenceBuf_ = sentenceBuf_.mid(idx + 1);
        if (!sentence.isEmpty() && tts_)
            tts_->speak(sentence, seqCounter_++);
    }
    // 超长没标点也切
    if (sentenceBuf_.size() > 42 && tts_) {
        tts_->speak(sentenceBuf_.trimmed(), seqCounter_++);
        sentenceBuf_.clear();
    }
}

void MainWindow::flushSentence(bool force)
{
    if (!sentenceBuf_.trimmed().isEmpty() && tts_) {
        tts_->speak(sentenceBuf_.trimmed(), seqCounter_++);
    }
    sentenceBuf_.clear();
    Q_UNUSED(force);
}

void MainWindow::resetAudioPipeline()
{
    player_.clear();
    player_.skipTo(++seqCounter_);
}
