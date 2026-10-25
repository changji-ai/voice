#include "LocalStt.h"
#include "util/Settings.h"
#include "../models/ModelManager.h"

#include <cstring>

extern "C" {
#include "c-api.h"
}

LocalStt::LocalStt(const AppSettings &cfg) : SttEngine(nullptr)
{
    const ModelDef def = ModelManager::find(cfg.sttLocal.modelKey);
    modelDir_ = modelsRoot() + "/" + def.key;
}

LocalStt::~LocalStt()
{
    stop();
}

void LocalStt::start()
{
    const QString dir = modelDir_;

    SherpaOnnxOnlineRecognizerConfig config;
    memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder =
        strdup(QString(dir + "/encoder-epoch-99-avg-1.int8.onnx").toUtf8().constData());
    config.model_config.transducer.decoder =
        strdup(QString(dir + "/decoder-epoch-99-avg-1.onnx").toUtf8().constData());
    config.model_config.transducer.joiner =
        strdup(QString(dir + "/joiner-epoch-99-avg-1.int8.onnx").toUtf8().constData());
    config.model_config.tokens = strdup(QString(dir + "/tokens.txt").toUtf8().constData());
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.decoding_method = "greedy_search";

    recognizer_ = SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer_) {
        emit errorOccurred("本地识别模型加载失败（模型文件不完整？可在设置中重新下载）");
        return;
    }
    stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
    emit engineReady();
}

void LocalStt::feed(const QByteArray &pcm)
{
    if (!recognizer_ || !stream_)
        return;
    const int n = pcm.size() / 2;
    QVector<float> samples(n);
    const auto *in = reinterpret_cast<const int16_t *>(pcm.constData());
    for (int i = 0; i < n; i++)
        samples[i] = in[i] / 32768.f;
    SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, samples.constData(), n);
    while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
        SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
        const SherpaOnnxOnlineRecognizerResult *r =
            SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
        if (r && r->text) {
            const QString text = QString::fromUtf8(r->text).trimmed();
            if (text != lastText_ && !text.isEmpty()) {
                lastText_ = text;
                emit partial(text);
            }
        }
        if (r)
            SherpaOnnxDestroyOnlineRecognizerResult(r);
    }
}

void LocalStt::notifyUtteranceEnd()
{
    if (!stream_)
        return;
    if (!lastText_.isEmpty())
        emit finalText(lastText_);
    lastText_.clear();
    SherpaOnnxOnlineStreamReset(recognizer_, stream_);
}

void LocalStt::stop()
{
    if (stream_) {
        SherpaOnnxOnlineStreamInputFinished(stream_);
        SherpaOnnxDestroyOnlineStream(stream_);
        stream_ = nullptr;
    }
    if (recognizer_) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
}
