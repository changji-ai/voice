#include "LocalTts.h"
#include "util/Settings.h"
#include "../models/ModelManager.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
#include <cstring>

extern "C" {
#include "c-api.h"
}

LocalTts::LocalTts(const AppSettings &cfg) : TtsEngine(nullptr)
{
    sid_ = cfg.ttsLocal.sid;
    speed_ = cfg.ttsLocal.speed;
    const ModelDef def = ModelManager::find(cfg.ttsLocal.modelKey);
    const QString dir = modelsRoot() + "/" + def.key;

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    config.model.kokoro.model = strdup(QString(dir + "/model.onnx").toUtf8().constData());
    config.model.kokoro.voices = strdup(QString(dir + "/voices.bin").toUtf8().constData());
    config.model.kokoro.tokens = strdup(QString(dir + "/tokens.txt").toUtf8().constData());
    config.model.kokoro.data_dir = strdup(QString(dir + "/espeak-ng-data").toUtf8().constData());
    // 官方用法：英文用 us 词表，中文用 zh 词表，逗号分隔
    config.model.kokoro.lexicon = strdup(
        QString(dir + "/lexicon-us-en.txt," + dir + "/lexicon-zh.txt").toUtf8().constData());
    config.model.kokoro.dict_dir = strdup(QString(dir + "/dict").toUtf8().constData());
    config.model.num_threads = 2;
    config.model.provider = "cpu";
    config.model.debug = 0;

    tts_ = SherpaOnnxCreateOfflineTts(&config);
    if (!tts_)
        qWarning("kokoro 模型加载失败: %s", qPrintable(dir));
}

LocalTts::~LocalTts()
{
    stop();
    if (tts_)
        SherpaOnnxDestroyOfflineTts(tts_);
}

bool LocalTts::modelReady(const AppSettings &cfg)
{
    const ModelDef def = ModelManager::find(cfg.ttsLocal.modelKey);
    if (!ModelManager::isReady(def))
        return false;
    const QString dir = modelsRoot() + "/" + def.key;
    return QFile::exists(dir + "/model.onnx") && QFile::exists(dir + "/voices.bin")
           && QFile::exists(dir + "/tokens.txt") && QFile::exists(dir + "/espeak-ng-data")
           && QFile::exists(dir + "/lexicon-us-en.txt") && QFile::exists(dir + "/lexicon-zh.txt")
           && QFile::exists(dir + "/dict");
}

void LocalTts::speak(const QString &text, int seq)
{
    if (!tts_) {
        emit errorOccurred("本地 TTS 模型未加载", seq);
        return;
    }
    mutex_.lock();
    queue_.enqueue(qMakePair(text, seq));
    mutex_.unlock();
    synthesizeNext();
}

void LocalTts::synthesizeNext()
{
    mutex_.lock();
    if (running_ || queue_.isEmpty()) {
        mutex_.unlock();
        return;
    }
    running_ = true;
    const auto job = queue_.dequeue();
    mutex_.unlock();

    const SherpaOnnxOfflineTts *tts = tts_;
    const int sid = sid_;
    const double speed = speed_;
    const QString text = job.first;
    const int seq = job.second;
    (void)QtConcurrent::run([this, tts, sid, speed, text, seq] {
        const QByteArray textUtf8 = text.toUtf8();
        const SherpaOnnxGeneratedAudio *audio =
            SherpaOnnxOfflineTtsGenerate(tts, textUtf8.constData(), sid, float(speed));
        QVector<float> samples;
        int sr = 24000;
        if (audio && audio->n > 0) {
            sr = audio->sample_rate;
            samples.resize(audio->n);
            memcpy(samples.data(), audio->samples, sizeof(float) * audio->n);
        }
        if (audio)
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        QMetaObject::invokeMethod(
            this,
            [this, samples, sr, seq] {
                if (!samples.isEmpty())
                    emit audioReady(samples, sr, seq);
                else
                    emit errorOccurred("本地合成返回空音频", seq);
                mutex_.lock();
                running_ = false;
                const bool more = !queue_.isEmpty() && !stopped_;
                mutex_.unlock();
                if (more)
                    synthesizeNext();
            },
            Qt::QueuedConnection);
    });
}

void LocalTts::stop()
{
    mutex_.lock();
    stopped_ = true;
    queue_.clear();
    mutex_.unlock();
}
