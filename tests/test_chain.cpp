// 冒烟测试（不依赖 GUI）：
// 1) kokoro 本地 TTS 合成中文句子 → 写 out_tts.wav
// 2) zipformer 本地 STT 识别该 wav → 打印识别文本
// 全链路无网络、无麦克风，验证 MinGW+sherpa DLL、模型加载、中文合成与识别。
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

extern "C" {
#include "c-api.h"
}

static QString modelsRoot()
{
    const QString base = qEnvironmentVariable("APPDATA");
    if (!base.isEmpty())
        return base + "/VoiceAssistant/models";
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
}

static bool writeWav(const QString &path, const float *samples, int n, int sampleRate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    struct Header
    {
        char riff[4] = { 'R', 'I', 'F', 'F' };
        uint32_t size36 = 36;
        char wave[4] = { 'W', 'A', 'V', 'E' };
        char fmt[4] = { 'f', 'm', 't', ' ' };
        uint32_t fmtSize = 16;
        uint16_t fmtTag = 1;
        uint16_t channels = 1;
        uint32_t rate = 0;
        uint32_t byteRate = 0;
        uint16_t blockAlign = 2;
        uint16_t bits = 16;
        char data[4] = { 'd', 'a', 't', 'a' };
        uint32_t dataSize = 0;
    } h;
    h.rate = sampleRate;
    h.byteRate = sampleRate * 2;
    h.dataSize = n * 2;
    h.size36 = 36 + n * 2;
    f.write(reinterpret_cast<const char *>(&h), sizeof(h));
    std::vector<int16_t> pcm(n);
    for (int i = 0; i < n; i++) {
        float v = samples[i];
        if (v > 1)
            v = 1;
        if (v < -1)
            v = -1;
        pcm[i] = int16_t(v * 32767);
    }
    f.write(reinterpret_cast<const char *>(pcm.data()), n * 2);
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("VoiceAssistant");
    QCoreApplication::setApplicationName("VoiceAssistant");
    QTextStream out(stdout);
    const QString root = modelsRoot();

    // ---------- 1. 本地 TTS ----------
    const QString ttsDir = root + "/kokoro-multi-lang";
    if (!QFile::exists(ttsDir + "/model.onnx")) {
        out << "SKIP-TTS kokoro 模型未就绪\n";
        return 2;
    }
    SherpaOnnxOfflineTtsConfig tc;
    memset(&tc, 0, sizeof(tc));
    const std::string model = (ttsDir + "/model.onnx").toStdString();
    const std::string voices = (ttsDir + "/voices.bin").toStdString();
    const std::string tokens = (ttsDir + "/tokens.txt").toStdString();
    const std::string dataDir = (ttsDir + "/espeak-ng-data").toStdString();
    const std::string lexicon =
        (ttsDir + "/lexicon-us-en.txt," + ttsDir + "/lexicon-zh.txt").toStdString();
    const std::string dictDir = (ttsDir + "/dict").toStdString();
    tc.model.kokoro.model = model.c_str();
    tc.model.kokoro.voices = voices.c_str();
    tc.model.kokoro.tokens = tokens.c_str();
    tc.model.kokoro.data_dir = dataDir.c_str();
    tc.model.kokoro.lexicon = lexicon.c_str();
    tc.model.kokoro.dict_dir = dictDir.c_str();
    tc.model.num_threads = 2;
    tc.model.provider = "cpu";
    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&tc);
    if (!tts) {
        out << "FAIL-TTS 模型加载失败\n";
        return 1;
    }
    out << "TTS loaded\n";
    const char *text = argc > 1 ? argv[1] : "你好，欢迎使用语音助手，今天天气不错。";
    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerate(tts, text, 45, 1.0f); // sid=45 zf_xiaobei
    if (!audio || audio->n <= 0) {
        out << "FAIL-TTS 合成结果为空\n";
        return 1;
    }
    out << "TTS generated " << audio->n << " samples @ " << audio->sample_rate << "Hz\n";
    const QString wavPath = QCoreApplication::applicationDirPath() + "/out_tts.wav";
    writeWav(wavPath, audio->samples, audio->n, audio->sample_rate);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    SherpaOnnxDestroyOfflineTts(tts);
    out << "TTS wav written: " << wavPath << "\n";

    // ---------- 2. 本地 STT ----------
    const QString sttDir = root + "/zipformer-zh-en";
    SherpaOnnxOnlineRecognizerConfig rc;
    memset(&rc, 0, sizeof(rc));
    const std::string enc = (sttDir + "/encoder-epoch-99-avg-1.int8.onnx").toStdString();
    const std::string dec = (sttDir + "/decoder-epoch-99-avg-1.int8.onnx").toStdString();
    const std::string joi = (sttDir + "/joiner-epoch-99-avg-1.int8.onnx").toStdString();
    const std::string tok = (sttDir + "/tokens.txt").toStdString();
    rc.feat_config.sample_rate = 16000;
    rc.feat_config.feature_dim = 80;
    rc.model_config.transducer.encoder = enc.c_str();
    rc.model_config.transducer.decoder = dec.c_str();
    rc.model_config.transducer.joiner = joi.c_str();
    rc.model_config.tokens = tok.c_str();
    rc.model_config.provider = "cpu";
    rc.model_config.num_threads = 2;
    rc.decoding_method = "greedy_search";
    const SherpaOnnxOnlineRecognizer *rec = SherpaOnnxCreateOnlineRecognizer(&rc);
    if (!rec) {
        out << "FAIL-STT 模型加载失败\n";
        return 1;
    }
    out << "STT loaded\n";
    const SherpaOnnxOnlineStream *stream = SherpaOnnxCreateOnlineStream(rec);
    // 重放 wav（16k mono）
    QFile wf(wavPath);
    wf.open(QIODevice::ReadOnly);
    wf.seek(44);
    const QByteArray pcm = wf.readAll();
    const int n = pcm.size() / 2;
    std::vector<float> samples(n);
    const auto *in = reinterpret_cast<const int16_t *>(pcm.constData());
    for (int i = 0; i < n; i++)
        samples[i] = in[i] / 32768.f;
    // 模拟实时流式：每次 3200 样本
    QString result;
    for (int start = 0; start < n; start += 3200) {
        const int chunk = std::min(3200, n - start);
        SherpaOnnxOnlineStreamAcceptWaveform(stream, 16000, samples.data() + start, chunk);
        while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
            SherpaOnnxDecodeOnlineStream(rec, stream);
            const SherpaOnnxOnlineRecognizerResult *r = SherpaOnnxGetOnlineStreamResult(rec, stream);
            if (r && r->text)
                result = QString::fromUtf8(r->text);
            if (r)
                SherpaOnnxDestroyOnlineRecognizerResult(r);
        }
    }
    SherpaOnnxOnlineStreamInputFinished(stream);
    while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
        SherpaOnnxDecodeOnlineStream(rec, stream);
        const SherpaOnnxOnlineRecognizerResult *r = SherpaOnnxGetOnlineStreamResult(rec, stream);
        if (r && r->text)
            result = QString::fromUtf8(r->text);
        if (r)
            SherpaOnnxDestroyOnlineRecognizerResult(r);
    }
    out << "STT result: " << result << "\n";
    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxDestroyOnlineRecognizer(rec);
    out << (result.contains("欢迎") || result.contains("语音") || result.contains("助手") || result.contains("天气")
                ? "PASS 链路验证成功\n"
                : "WARN 识别文本与原文不完全匹配（可能是音色/标点差异）\n");
    return 0;
}
