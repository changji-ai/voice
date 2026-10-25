#include "SettingsDialog.h"
#include "util/Settings.h"
#include "models/ModelManager.h"
#include "clone/VoiceClone.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileInfo>
#include <QFrame>

static QWidget *makeDropArea(QLabel **hintOut, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet("QFrame { border: 2px dashed #888; border-radius: 8px; }");
    frame->setAcceptDrops(true);
    auto *lay = new QVBoxLayout(frame);
    auto *hint = new QLabel("把视频或音频文件拖到这里\n（提取前 25 秒声音用于复刻）", frame);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("border: none; color: #666;");
    lay->addWidget(hint);
    frame->installEventFilter(parent);
    *hintOut = hint;
    return frame;
}

SettingsDialog::SettingsDialog(AppSettings *cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle("设置");
    setMinimumSize(560, 520);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("mainTabs");
    tabs->addTab(buildSttPage(), "语音识别");
    tabs->addTab(buildTtsPage(), "语音合成");
    tabs->addTab(buildLlmPage(), "对话模型");
    tabs->addTab(buildClonePage(), "音色克隆");
    root->addWidget(tabs);

    auto *btns = new QHBoxLayout;
    auto *ok = new QPushButton("保存");
    auto *cancel = new QPushButton("取消");
    btns->addStretch(1);
    btns->addWidget(ok);
    btns->addWidget(cancel);
    root->addLayout(btns);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    loadUi();
}

bool SettingsDialog::eventFilter(QObject *obj, QEvent *ev)
{
    // 拖拽克隆区
    if (obj == dropArea_ && ev->type() == QEvent::DragEnter) {
        auto *de = static_cast<QDragEnterEvent *>(ev);
        if (de->mimeData()->hasUrls())
            de->acceptProposedAction();
        return true;
    }
    if (obj == dropArea_ && ev->type() == QEvent::Drop) {
        auto *de = static_cast<QDropEvent *>(ev);
        const auto urls = de->mimeData()->urls();
        if (!urls.isEmpty()) {
            droppedFile_ = urls.first().toLocalFile();
            dropHint_->setText("已选择：" + QFileInfo(droppedFile_).fileName());
        }
        return true;
    }
    return QDialog::eventFilter(obj, ev);
}

void SettingsDialog::selectTab(int i)
{
    if (auto *t = findChild<QTabWidget *>("mainTabs"))
        t->setCurrentIndex(i);
}

QWidget *SettingsDialog::buildSttPage()
{
    auto *w = new QWidget(this);
    auto *form = new QFormLayout(w);

    sttEngine_ = new QComboBox(w);
    sttEngine_->addItem("本地（离线、免费，推荐）", "local");
    sttEngine_->addItem("Paraformer 实时（阿里云，需 Key）", "paraformer");
    sttEngine_->addItem("OpenAI 兼容分段（whisper 等，需 Key）", "openai");
    form->addRow("识别引擎", sttEngine_);

    sttStack_ = new QStackedWidget(w);
    form->addRow(sttStack_);

    // 本地页
    {
        auto *p = new QWidget(sttStack_);
        auto *f = new QFormLayout(p);
        sttModelStatus_ = new QLabel(p);
        f->addRow("本地模型", sttModelStatus_);
        auto *h = new QHBoxLayout;
        sttDlBtn_ = new QPushButton("下载 / 修复模型（约 310MB，含中英）", p);
        sttDlBar_ = new QProgressBar(p);
        sttDlBar_->setValue(0);
        sttDlBar_->setMaximumWidth(160);
        h->addWidget(sttDlBtn_);
        h->addWidget(sttDlBar_);
        f->addRow("", h);
        sttSilenceMs_ = new QSpinBox(p);
        sttSilenceMs_->setRange(300, 2000);
        sttSilenceMs_->setSingleStep(100);
        sttSilenceMs_->setSuffix(" ms");
        f->addRow("静音多久算一句话", sttSilenceMs_);
        sttSensitivity_ = new QComboBox(p);
        sttSensitivity_->addItem("高（安静环境）", 0.005);
        sttSensitivity_->addItem("中（默认）", 0.008);
        sttSensitivity_->addItem("低（嘈杂环境）", 0.015);
        f->addRow("麦克风灵敏度", sttSensitivity_);
        sttStack_->addWidget(p);
        connect(sttDlBtn_, &QPushButton::clicked, this, [this] {
            startModelDownload(ModelManager::find(cfg_->sttLocal.modelKey), sttDlBtn_, sttDlBar_, sttModelStatus_);
        });
    }
    // Paraformer 页
    {
        auto *p = new QWidget(sttStack_);
        auto *f = new QFormLayout(p);
        paraKey_ = new QLineEdit(p);
        paraKey_->setEchoMode(QLineEdit::Password);
        paraKey_->setPlaceholderText("DashScope API Key（sk-开头）");
        f->addRow("API Key", paraKey_);
        paraModel_ = new QLineEdit(p);
        f->addRow("模型", paraModel_);
        paraDomain_ = new QLineEdit(p);
        f->addRow("域名", paraDomain_);
        paraLang_ = new QLineEdit(p);
        paraLang_->setPlaceholderText("留空=自动检测；如 zh、en");
        f->addRow("语言", paraLang_);
        sttStack_->addWidget(p);
    }
    // OpenAI 页
    {
        auto *p = new QWidget(sttStack_);
        auto *f = new QFormLayout(p);
        oaiSttUrl_ = new QLineEdit(p);
        f->addRow("Base URL", oaiSttUrl_);
        oaiSttKey_ = new QLineEdit(p);
        oaiSttKey_->setEchoMode(QLineEdit::Password);
        f->addRow("API Key", oaiSttKey_);
        oaiSttModel_ = new QLineEdit(p);
        f->addRow("模型", oaiSttModel_);
        oaiSttLang_ = new QLineEdit(p);
        oaiSttLang_->setPlaceholderText("可选，如 zh");
        f->addRow("语言", oaiSttLang_);
        oaiSttSilenceMs_ = new QSpinBox(p);
        oaiSttSilenceMs_->setRange(300, 2000);
        oaiSttSilenceMs_->setSingleStep(100);
        f->addRow("静音多久算一句话", oaiSttSilenceMs_);
        sttStack_->addWidget(p);
    }
    connect(sttEngine_, &QComboBox::currentIndexChanged, sttStack_, &QStackedWidget::setCurrentIndex);
    return w;
}

QWidget *SettingsDialog::buildTtsPage()
{
    auto *w = new QWidget(this);
    auto *form = new QFormLayout(w);

    ttsEngine_ = new QComboBox(w);
    ttsEngine_->addItem("本地（离线、免费，kokoro 82 音色，推荐）", "local");
    ttsEngine_->addItem("CosyVoice（阿里云，支持克隆音色）", "cosyvoice");
    ttsEngine_->addItem("OpenAI 兼容", "openai");
    form->addRow("合成引擎", ttsEngine_);

    ttsStack_ = new QStackedWidget(w);
    form->addRow(ttsStack_);

    // 本地页
    {
        auto *p = new QWidget(ttsStack_);
        auto *f = new QFormLayout(p);
        ttsModelStatus_ = new QLabel(p);
        f->addRow("本地模型", ttsModelStatus_);
        auto *h = new QHBoxLayout;
        ttsDlBtn_ = new QPushButton("下载 / 修复模型（约 330MB，中英 82 音色）", p);
        ttsDlBar_ = new QProgressBar(p);
        ttsDlBar_->setValue(0);
        ttsDlBar_->setMaximumWidth(160);
        h->addWidget(ttsDlBtn_);
        h->addWidget(ttsDlBar_);
        f->addRow("", h);
        ttsLocalSid_ = new QComboBox(p);
        // kokoro-multi-lang-v1_0 官方音色表（ID→名字，45-52 为中文音色）
        const QVector<QPair<int, QString>> voices = {
            { 45, "小贝 zf_xiaobei（中·女）" }, { 46, "小妮 zf_xiaoni（中·女）" },
            { 47, "小晓 zf_xiaoxiao（中·女）" }, { 48, "小艺 zf_xiaoyi（中·女）" },
            { 49, "云健 zm_yunjian（中·男）" }, { 50, "云希 zm_yunxi（中·男）" },
            { 51, "云夏 zm_yunxia（中·男）" }, { 52, "云扬 zm_yunyang（中·男）" },
            { 0, "af_alloy（英·女）" }, { 1, "af_aoede（英·女）" }, { 2, "af_bella（英·女）" },
            { 3, "af_heart（英·女）" }, { 4, "af_jessica（英·女）" }, { 5, "af_kore（英·女）" },
            { 6, "af_nicole（英·女）" }, { 7, "af_nova（英·女）" }, { 8, "af_river（英·女）" },
            { 9, "af_sarah（英·女）" }, { 10, "af_sky（英·女）" },
            { 11, "am_adam（英·男）" }, { 12, "am_echo（英·男）" }, { 13, "am_eric（英·男）" },
            { 14, "am_fenrir（英·男）" }, { 15, "am_liam（英·男）" }, { 16, "am_michael（英·男）" },
            { 17, "am_onyx（英·男）" }, { 18, "am_puck（英·男）" }, { 19, "am_santa（英·男）" },
            { 20, "bf_alice（英·女·英音）" }, { 21, "bf_emma（英·女·英音）" },
            { 22, "bf_isabella（英·女·英音）" }, { 23, "bf_lily（英·女·英音）" },
            { 24, "bm_daniel（英·男·英音）" }, { 25, "bm_fable（英·男·英音）" },
            { 26, "bm_george（英·男·英音）" }, { 27, "bm_lewis（英·男·英音）" }
        };
        for (const auto &v : voices)
            ttsLocalSid_->addItem(v.second, v.first);
        f->addRow("音色", ttsLocalSid_);
        auto *h2 = new QHBoxLayout;
        ttsLocalSpeed_ = new QSlider(Qt::Horizontal, p);
        ttsLocalSpeed_->setRange(50, 200);
        ttsLocalSpeedLabel_ = new QLabel("1.00x", p);
        h2->addWidget(ttsLocalSpeed_);
        h2->addWidget(ttsLocalSpeedLabel_);
        f->addRow("语速", h2);
        connect(ttsLocalSpeed_, &QSlider::valueChanged, this, [this](int v) {
            ttsLocalSpeedLabel_->setText(QString("%1x").arg(v / 100.0, 0, 'f', 2));
        });
        ttsStack_->addWidget(p);
        connect(ttsDlBtn_, &QPushButton::clicked, this, [this] {
            startModelDownload(ModelManager::find(cfg_->ttsLocal.modelKey), ttsDlBtn_, ttsDlBar_, ttsModelStatus_);
        });
    }
    // CosyVoice 页
    {
        auto *p = new QWidget(ttsStack_);
        auto *f = new QFormLayout(p);
        cosyKey_ = new QLineEdit(p);
        cosyKey_->setEchoMode(QLineEdit::Password);
        cosyKey_->setPlaceholderText("DashScope API Key（sk-开头）");
        f->addRow("API Key", cosyKey_);
        cosyModel_ = new QLineEdit(p);
        f->addRow("模型", cosyModel_);
        cosyVoice_ = new QComboBox(p);
        cosyVoice_->setEditable(true);
        f->addRow("音色", cosyVoice_);
        auto mkSlider = [&](QSlider *&sl, QLabel *&lb, int min, int max, int def, double div) {
            auto *h = new QHBoxLayout;
            sl = new QSlider(Qt::Horizontal, p);
            sl->setRange(min, max);
            sl->setValue(def);
            lb = new QLabel(p);
            h->addWidget(sl);
            h->addWidget(lb);
            connect(sl, &QSlider::valueChanged, this, [lb, div](int v) {
                lb->setText(QString("%1").arg(v / div, 0, 'f', 2));
            });
            lb->setText(QString("%1").arg(def / div, 0, 'f', 2));
            return h;
        };
        f->addRow("语速 (0.5~2)", mkSlider(cosyRate_, cosyRateLabel_, 50, 200, 100, 100));
        f->addRow("音调 (0.5~2)", mkSlider(cosyPitch_, cosyPitchLabel_, 50, 200, 100, 100));
        f->addRow("音量 (0~100)", mkSlider(cosyVolume_, cosyVolumeLabel_, 0, 100, 50, 1));
        ttsStack_->addWidget(p);
    }
    // OpenAI 页
    {
        auto *p = new QWidget(ttsStack_);
        auto *f = new QFormLayout(p);
        oaiTtsUrl_ = new QLineEdit(p);
        f->addRow("Base URL", oaiTtsUrl_);
        oaiTtsKey_ = new QLineEdit(p);
        oaiTtsKey_->setEchoMode(QLineEdit::Password);
        f->addRow("API Key", oaiTtsKey_);
        oaiTtsModel_ = new QLineEdit(p);
        f->addRow("模型", oaiTtsModel_);
        oaiTtsVoice_ = new QLineEdit(p);
        f->addRow("音色", oaiTtsVoice_);
        auto *h = new QHBoxLayout;
        oaiTtsSpeed_ = new QSlider(Qt::Horizontal, p);
        oaiTtsSpeed_->setRange(25, 400);
        oaiTtsSpeed_->setValue(100);
        oaiTtsSpeedLabel_ = new QLabel("1.00x", p);
        h->addWidget(oaiTtsSpeed_);
        h->addWidget(oaiTtsSpeedLabel_);
        connect(oaiTtsSpeed_, &QSlider::valueChanged, this, [this](int v) {
            oaiTtsSpeedLabel_->setText(QString("%1x").arg(v / 100.0, 0, 'f', 2));
        });
        f->addRow("语速", h);
        ttsStack_->addWidget(p);
    }

    pauseStt_ = new QCheckBox("播放回答时暂停收音（防止自问自答）", w);
    pauseStt_->setChecked(true);
    form->addRow("", pauseStt_);

    connect(ttsEngine_, &QComboBox::currentIndexChanged, ttsStack_, &QStackedWidget::setCurrentIndex);
    return w;
}

QWidget *SettingsDialog::buildLlmPage()
{
    auto *w = new QWidget(this);
    auto *f = new QFormLayout(w);
    llmUrl_ = new QLineEdit(w);
    llmUrl_->setPlaceholderText("https://dashscope.aliyuncs.com/compatible-mode/v1 或任意 OpenAI 兼容地址");
    f->addRow("Base URL", llmUrl_);
    llmKey_ = new QLineEdit(w);
    llmKey_->setEchoMode(QLineEdit::Password);
    f->addRow("API Key", llmKey_);
    llmModel_ = new QLineEdit(w);
    f->addRow("模型", llmModel_);
    llmPrompt_ = new QPlainTextEdit(w);
    llmPrompt_->setMaximumHeight(110);
    f->addRow("系统提示词", llmPrompt_);
    llmTemp_ = new QDoubleSpinBox(w);
    llmTemp_->setRange(0, 2);
    llmTemp_->setSingleStep(0.1);
    f->addRow("温度", llmTemp_);
    llmHistory_ = new QSpinBox(w);
    llmHistory_->setRange(0, 30);
    f->addRow("保留历史轮数", llmHistory_);
    return w;
}

QWidget *SettingsDialog::buildClonePage()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);

    auto *note = new QLabel("克隆音色使用阿里云百炼“声音复刻”：拖入一段视频/音频（建议 10 秒以上清晰人声），"
                            "需要已配置 CosyVoice 的 API Key。克隆后即可在“语音合成”页选择该音色。", w);
    note->setWordWrap(true);
    v->addWidget(note);

    dropArea_ = makeDropArea(&dropHint_, w);
    v->addWidget(dropArea_);

    auto *h = new QHBoxLayout;
    cloneName_ = new QLineEdit(w);
    cloneName_->setPlaceholderText("给这个音色起个名字（如：我的声音）");
    cloneBtn_ = new QPushButton("开始克隆", w);
    h->addWidget(cloneName_, 1);
    h->addWidget(cloneBtn_);
    v->addLayout(h);

    cloneStatus_ = new QLabel(w);
    cloneStatus_->setWordWrap(true);
    v->addWidget(cloneStatus_);

    v->addWidget(new QLabel("我的克隆音色：", w));
    cloneList_ = new QListWidget(w);
    v->addWidget(cloneList_, 1);
    auto *btns = new QHBoxLayout;
    auto *useBtn = new QPushButton("选用（设为 CosyVoice 音色）", w);
    auto *delBtn = new QPushButton("删除", w);
    auto *refreshBtn = new QPushButton("从云端刷新", w);
    btns->addWidget(useBtn);
    btns->addWidget(delBtn);
    btns->addStretch(1);
    btns->addWidget(refreshBtn);
    v->addLayout(btns);

    connect(cloneBtn_, &QPushButton::clicked, this, [this] {
        if (droppedFile_.isEmpty()) {
            cloneStatus_->setText("请先把视频/音频文件拖到上方区域");
            return;
        }
        if (cfg_->ttsCosy.apiKey.isEmpty()) {
            cloneStatus_->setText("请先在“语音合成”页填好 CosyVoice API Key");
            return;
        }
        cloneBtn_->setEnabled(false);
        static int cloneIndex = 0;
        auto *vc = new VoiceClone(this);
        vc->setConfig(cfg_);
        connect(vc, &VoiceClone::cloneProgress, this, [this](const QString &s) { cloneStatus_->setText(s); });
        connect(vc, &VoiceClone::cloneDone, this, [this, vc](const ClonedVoice &voice, const QString &err) {
            vc->deleteLater();
            cloneBtn_->setEnabled(true);
            if (err.isEmpty()) {
                cfg_->clonedVoices.append(voice);
                cfg_->save();
                cloneStatus_->setText("克隆成功：" + voice.voiceId);
                dropHint_->setText("把视频或音频文件拖到这里");
                droppedFile_.clear();
                loadUi();
            } else {
                cloneStatus_->setText("克隆失败：" + err);
            }
        });
        vc->cloneFromFile(droppedFile_, cloneName_->text().trimmed().isEmpty() ? "myvoice" : cloneName_->text().trimmed(), cloneIndex++);
    });
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refreshCloneList(); });
    connect(useBtn, &QPushButton::clicked, this, [this] {
        const auto item = cloneList_->currentItem();
        if (!item)
            return;
        const QString vid = item->data(Qt::UserRole).toString();
        cfg_->ttsCosy.voice = vid;
        saveUi();
        refreshCloneList();
        cloneStatus_->setText("已选用音色 " + vid + "（记得点保存）");
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        const auto item = cloneList_->currentItem();
        if (!item)
            return;
        const QString vid = item->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, "删除音色", "确定删除克隆音色 " + vid + " ？") != QMessageBox::Yes)
            return;
        auto *vc = new VoiceClone(this);
        vc->setConfig(cfg_);
        connect(vc, &VoiceClone::deleteDone, this, [this, vc, vid](const QString &, const QString &err) {
            vc->deleteLater();
            for (int i = 0; i < cfg_->clonedVoices.size(); i++)
                if (cfg_->clonedVoices[i].voiceId == vid)
                    cfg_->clonedVoices.removeAt(i--);
            cfg_->save();
            loadUi();
            refreshCloneList();
            if (!err.isEmpty())
                cloneStatus_->setText("云端删除失败：" + err);
        });
        vc->deleteVoice(vid);
    });
    return w;
}

void SettingsDialog::refreshCloneList()
{
    cloneList_->clear();
    for (const auto &v : cfg_->clonedVoices) {
        auto *item = new QListWidgetItem(QString("%1  %2  %3").arg(v.name, v.voiceId, v.createdAt));
        item->setData(Qt::UserRole, v.voiceId);
        cloneList_->addItem(item);
    }
    // 云端刷新（如果有 key）
    if (!cfg_->ttsCosy.apiKey.isEmpty()) {
        auto *vc = new VoiceClone(this);
        vc->setConfig(cfg_);
        connect(vc, &VoiceClone::listDone, this, [this, vc](const QList<ClonedVoice> &voices, const QString &err) {
            vc->deleteLater();
            if (!err.isEmpty()) {
                cloneStatus_->setText("云端音色列表获取失败：" + err);
                return;
            }
            // 合并云端（补充本地没有的）
            for (const auto &v : voices) {
                bool found = false;
                for (const auto &local : cfg_->clonedVoices)
                    if (local.voiceId == v.voiceId)
                        found = true;
                if (!found)
                    cfg_->clonedVoices.append(v);
            }
            cfg_->save();
            cloneList_->clear();
            for (const auto &v : cfg_->clonedVoices) {
                auto *item = new QListWidgetItem(QString("%1  %2  %3").arg(v.name, v.voiceId, v.createdAt));
                item->setData(Qt::UserRole, v.voiceId);
                cloneList_->addItem(item);
            }
        });
        vc->listVoices();
    }
}

void SettingsDialog::startModelDownload(const ModelDef &def, QPushButton *btn, QProgressBar *bar, QLabel *status)
{
    if (ModelManager::isReady(def)) {
        if (QMessageBox::question(this, "重新下载", "模型已就绪，要重新下载吗？") != QMessageBox::Yes)
            return;
        QFile::remove(modelsRoot() + "/" + def.key + "/done.marker");
    }
    btn->setEnabled(false);
    bar->setValue(0);
    status->setText("开始下载…");
    auto *mgr = new ModelManager(this);
    connect(mgr, &ModelManager::progress, this, [bar, status](int pct, const QString &file) {
        bar->setValue(pct);
        status->setText(QString("下载中 %1%（%2）").arg(pct).arg(QFileInfo(file).fileName()));
    });
    connect(mgr, &ModelManager::finished, this, [this, mgr, def, btn, bar, status](bool ok, const QString &err) {
        mgr->deleteLater();
        btn->setEnabled(true);
        if (ok) {
            bar->setValue(100);
            updateSttModelStatus();
            updateTtsModelStatus();
        } else {
            status->setText("下载失败：" + err);
        }
    });
    mgr->download(def);
}

void SettingsDialog::updateSttModelStatus()
{
    const ModelDef def = ModelManager::find(cfg_->sttLocal.modelKey);
    if (ModelManager::isReady(def))
        sttModelStatus_->setText("✅ 已就绪（" + def.label + "）");
    else
        sttModelStatus_->setText("❌ 未下载（" + def.label + "）");
}

void SettingsDialog::updateTtsModelStatus()
{
    const ModelDef def = ModelManager::find(cfg_->ttsLocal.modelKey);
    if (ModelManager::isReady(def))
        ttsModelStatus_->setText("✅ 已就绪（" + def.label + "）");
    else
        ttsModelStatus_->setText("❌ 未下载（" + def.label + "）");
}

void SettingsDialog::loadUi()
{
    const int sttIdx = sttEngine_->findData(cfg_->sttEngine);
    sttEngine_->setCurrentIndex(sttIdx >= 0 ? sttIdx : 0);
    sttStack_->setCurrentIndex(sttEngine_->currentIndex());
    sttSilenceMs_->setValue(cfg_->sttLocal.silenceMs);
    const int sensIdx = sttSensitivity_->findData(cfg_->sttLocal.vadThreshold);
    sttSensitivity_->setCurrentIndex(sensIdx >= 0 ? sensIdx : 1);
    paraKey_->setText(cfg_->sttParaformer.apiKey);
    paraModel_->setText(cfg_->sttParaformer.model);
    paraDomain_->setText(cfg_->sttParaformer.domain);
    paraLang_->setText(cfg_->sttParaformer.language);
    oaiSttUrl_->setText(cfg_->sttOpenai.baseURL);
    oaiSttKey_->setText(cfg_->sttOpenai.apiKey);
    oaiSttModel_->setText(cfg_->sttOpenai.model);
    oaiSttLang_->setText(cfg_->sttOpenai.language);
    oaiSttSilenceMs_->setValue(cfg_->sttOpenai.silenceMs);

    const int ttsIdx = ttsEngine_->findData(cfg_->ttsEngine);
    ttsEngine_->setCurrentIndex(ttsIdx >= 0 ? ttsIdx : 0);
    ttsStack_->setCurrentIndex(ttsEngine_->currentIndex());
    ttsLocalSid_->setCurrentIndex(0);
    for (int i = 0; i < ttsLocalSid_->count(); i++)
        if (ttsLocalSid_->itemData(i).toInt() == cfg_->ttsLocal.sid)
            ttsLocalSid_->setCurrentIndex(i);
    ttsLocalSpeed_->setValue(int(cfg_->ttsLocal.speed * 100));
    ttsLocalSpeedLabel_->setText(QString("%1x").arg(cfg_->ttsLocal.speed, 0, 'f', 2));
    cosyKey_->setText(cfg_->ttsCosy.apiKey);
    cosyModel_->setText(cfg_->ttsCosy.model);
    cosyRate_->setValue(int(cfg_->ttsCosy.rate * 100));
    cosyPitch_->setValue(int(cfg_->ttsCosy.pitch * 100));
    cosyVolume_->setValue(cfg_->ttsCosy.volume);
    oaiTtsUrl_->setText(cfg_->ttsOpenai.baseURL);
    oaiTtsKey_->setText(cfg_->ttsOpenai.apiKey);
    oaiTtsModel_->setText(cfg_->ttsOpenai.model);
    oaiTtsVoice_->setText(cfg_->ttsOpenai.voice);
    oaiTtsSpeed_->setValue(int(cfg_->ttsOpenai.speed * 100));
    pauseStt_->setChecked(cfg_->pauseSttWhileSpeaking);

    // CosyVoice 音色下拉：内置 + 克隆
    cosyVoice_->clear();
    const QVector<QPair<QString, QString>> builtin = {
        { "longxiaochun_v2", "小淳（女）" }, { "longwan_v2", "小万（女）" }, { "longcheng_v2", "小诚（男）" },
        { "longxiao_v2", "小说（男）" }, { "longshu_v2", "小书（女）" }, { "longsheng_v2", "小盛（男）" },
        { "loongstella_v2", "Stella（女）" }, { "loongbella_v2", "Bella（女）" }
    };
    for (const auto &b : builtin)
        cosyVoice_->addItem(b.second + " (" + b.first + ")", b.first);
    for (const auto &v : cfg_->clonedVoices)
        cosyVoice_->addItem("🧬 " + (v.name.isEmpty() ? v.voiceId : v.name), v.voiceId);
    const int cvIdx = cosyVoice_->findData(cfg_->ttsCosy.voice);
    if (cvIdx >= 0)
        cosyVoice_->setCurrentIndex(cvIdx);
    else {
        cosyVoice_->setCurrentText(cfg_->ttsCosy.voice);
    }

    llmUrl_->setText(cfg_->llmBaseURL);
    llmKey_->setText(cfg_->llmApiKey);
    llmModel_->setText(cfg_->llmModel);
    llmPrompt_->setPlainText(cfg_->llmSystemPrompt);
    llmTemp_->setValue(cfg_->llmTemperature);
    llmHistory_->setValue(cfg_->llmHistoryTurns);

    updateSttModelStatus();
    updateTtsModelStatus();
    refreshCloneList();
}

void SettingsDialog::saveUi()
{
    cfg_->sttEngine = sttEngine_->currentData().toString();
    cfg_->sttLocal.silenceMs = sttSilenceMs_->value();
    cfg_->sttLocal.vadThreshold = sttSensitivity_->currentData().toDouble();
    cfg_->sttParaformer.apiKey = paraKey_->text().trimmed();
    cfg_->sttParaformer.model = paraModel_->text().trimmed();
    cfg_->sttParaformer.domain = paraDomain_->text().trimmed();
    cfg_->sttParaformer.language = paraLang_->text().trimmed();
    cfg_->sttOpenai.baseURL = oaiSttUrl_->text().trimmed();
    cfg_->sttOpenai.apiKey = oaiSttKey_->text().trimmed();
    cfg_->sttOpenai.model = oaiSttModel_->text().trimmed();
    cfg_->sttOpenai.language = oaiSttLang_->text().trimmed();
    cfg_->sttOpenai.silenceMs = oaiSttSilenceMs_->value();

    cfg_->ttsEngine = ttsEngine_->currentData().toString();
    cfg_->ttsLocal.sid = ttsLocalSid_->currentData().toInt();
    cfg_->ttsLocal.speed = ttsLocalSpeed_->value() / 100.0;
    cfg_->ttsCosy.apiKey = cosyKey_->text().trimmed();
    cfg_->ttsCosy.model = cosyModel_->text().trimmed();
    cfg_->ttsCosy.voice = cosyVoice_->currentData().isValid() ? cosyVoice_->currentData().toString()
                                                              : cosyVoice_->currentText().trimmed();
    cfg_->ttsCosy.rate = cosyRate_->value() / 100.0;
    cfg_->ttsCosy.pitch = cosyPitch_->value() / 100.0;
    cfg_->ttsCosy.volume = cosyVolume_->value();
    cfg_->ttsOpenai.baseURL = oaiTtsUrl_->text().trimmed();
    cfg_->ttsOpenai.apiKey = oaiTtsKey_->text().trimmed();
    cfg_->ttsOpenai.model = oaiTtsModel_->text().trimmed();
    cfg_->ttsOpenai.voice = oaiTtsVoice_->text().trimmed();
    cfg_->ttsOpenai.speed = oaiTtsSpeed_->value() / 100.0;
    cfg_->pauseSttWhileSpeaking = pauseStt_->isChecked();

    cfg_->llmBaseURL = llmUrl_->text().trimmed();
    cfg_->llmApiKey = llmKey_->text().trimmed();
    cfg_->llmModel = llmModel_->text().trimmed();
    cfg_->llmSystemPrompt = llmPrompt_->toPlainText();
    cfg_->llmTemperature = llmTemp_->value();
    cfg_->llmHistoryTurns = llmHistory_->value();

    cfg_->save();
}

void SettingsDialog::accept()
{
    saveUi();
    QDialog::accept();
}
