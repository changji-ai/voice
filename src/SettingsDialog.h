#pragma once

#include <QDialog>
#include <QVector>

struct AppSettings;
struct ClonedVoice;
struct ModelDef;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QProgressBar;
class QLabel;
class QPushButton;
class QStackedWidget;
class QListWidget;
class QCheckBox;
class QPlainTextEdit;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(AppSettings *cfg, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void accept() override;

private:
    QWidget *buildSttPage();
    QWidget *buildTtsPage();
    QWidget *buildLlmPage();
    QWidget *buildClonePage();
    void loadUi();
    void saveUi();
    void refreshCloneList();

public:
    void selectTab(int i);
    void updateSttModelStatus();
    void updateTtsModelStatus();
    void startModelDownload(const ModelDef &def, QPushButton *btn, QProgressBar *bar, QLabel *status);

    AppSettings *cfg_;

    // STT
    QComboBox *sttEngine_ = nullptr;
    QStackedWidget *sttStack_ = nullptr;
    QLabel *sttModelStatus_ = nullptr;
    QPushButton *sttDlBtn_ = nullptr;
    QProgressBar *sttDlBar_ = nullptr;
    QSpinBox *sttSilenceMs_ = nullptr;
    QComboBox *sttSensitivity_ = nullptr;
    QLineEdit *paraKey_ = nullptr, *paraModel_ = nullptr, *paraDomain_ = nullptr, *paraLang_ = nullptr;
    QLineEdit *oaiSttUrl_ = nullptr, *oaiSttKey_ = nullptr, *oaiSttModel_ = nullptr, *oaiSttLang_ = nullptr;
    QSpinBox *oaiSttSilenceMs_ = nullptr;

    // TTS
    QComboBox *ttsEngine_ = nullptr;
    QStackedWidget *ttsStack_ = nullptr;
    QLabel *ttsModelStatus_ = nullptr;
    QPushButton *ttsDlBtn_ = nullptr;
    QProgressBar *ttsDlBar_ = nullptr;
    QComboBox *ttsLocalSid_ = nullptr;
    QSlider *ttsLocalSpeed_ = nullptr;
    QLabel *ttsLocalSpeedLabel_ = nullptr;
    QLineEdit *cosyKey_ = nullptr, *cosyModel_ = nullptr;
    QComboBox *cosyVoice_ = nullptr;
    QSlider *cosyRate_ = nullptr, *cosyPitch_ = nullptr, *cosyVolume_ = nullptr;
    QLabel *cosyRateLabel_ = nullptr, *cosyPitchLabel_ = nullptr, *cosyVolumeLabel_ = nullptr;
    QLineEdit *oaiTtsUrl_ = nullptr, *oaiTtsKey_ = nullptr, *oaiTtsModel_ = nullptr, *oaiTtsVoice_ = nullptr;
    QSlider *oaiTtsSpeed_ = nullptr;
    QLabel *oaiTtsSpeedLabel_ = nullptr;
    QCheckBox *pauseStt_ = nullptr;

    // LLM
    QLineEdit *llmUrl_ = nullptr, *llmKey_ = nullptr, *llmModel_ = nullptr;
    QPlainTextEdit *llmPrompt_ = nullptr;
    QDoubleSpinBox *llmTemp_ = nullptr;
    QSpinBox *llmHistory_ = nullptr;

    // Clone
    QWidget *dropArea_ = nullptr;
    QLabel *dropHint_ = nullptr;
    QString droppedFile_;
    QLineEdit *cloneName_ = nullptr;
    QPushButton *cloneBtn_ = nullptr;
    QLabel *cloneStatus_ = nullptr;
    QListWidget *cloneList_ = nullptr;
};
