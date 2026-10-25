#include "Vad.h"

void Vad::feed(const int16_t *samples, int n)
{
    if (n <= 0)
        return;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        const double v = samples[i] / 32768.0;
        sum += v * v;
    }
    const double rms = std::sqrt(sum / n);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastTs_ == 0)
        lastTs_ = now;
    const qint64 chunkMs = 1000.0 * n / 16000.0;
    lastTs_ = now;

    if (rms >= threshold_) {
        silenceMsRun_ = 0;
        if (!inSpeech_) {
            inSpeech_ = true;
            speechMs_ = 0;
            emit utteranceStarted();
        }
        speechMs_ += chunkMs;
    } else if (inSpeech_) {
        silenceMsRun_ += chunkMs;
        if (silenceMsRun_ >= silenceMs_) {
            inSpeech_ = false;
            silenceMsRun_ = 0;
            emit utteranceEnded();
        }
    }
}

void Vad::reset()
{
    inSpeech_ = false;
    silenceMsRun_ = 0;
    speechMs_ = 0;
}
