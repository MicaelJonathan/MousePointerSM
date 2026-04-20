#pragma once
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>

static constexpr int SAMPLE_RATE = 44100;
static constexpr int BLOCK_SAMPLES = 441;
static constexpr int BLOCK_COUNT = 8;

class SoundDriver {
public:
    SoundDriver();
    ~SoundDriver();

    bool open();
    void close();
    void setVelocity(float velocityPxPerSec, bool pressed);

    void setMuted(bool muted) { muted_ = muted; }
    bool isMuted() const { return muted_; }

    // Parâmetros editáveis
    float attackSpeed = 0.050f;
    float decaySpeed = 0.030f;
    float sampleSpeed = 1.0f; // 0.0 ~ 2.0
    float volume = 5.0f; // 0.0 ~ 10.0

    bool loadSample(const std::string& path);
    void useSample(bool enable) { useSample_ = enable; }


private:
    static DWORD WINAPI audioThreadProc(LPVOID param);
    void audioLoop();
    void generateBlock(int16_t* buffer, int samples);
    volatile DWORD lastMoveTime_ = 0;

    volatile bool muted_ = true; // Inicia mutado por padrão.

    float nextSampleFrame(float speed);

    HWAVEOUT waveOut_ = nullptr;
    WAVEHDR  headers_[BLOCK_COUNT] = {};
    int16_t* buffers_[BLOCK_COUNT] = {};
    HANDLE   thread_ = nullptr;
    HANDLE   freeEvent_ = nullptr;
    bool     running_ = false;

    volatile float velocity_ = 0.0f;
    volatile bool  pressed_ = false;

    // Síntese
    float filterState_ = 0.0f;
    float envelopeVal_ = 0.0f;
    float envelopeTarget_ = 0.0f;

    // Sample
    std::vector<float> sampleData_; 
    float              loopPos_ = 0.0f;
    bool               useSample_ = false;
};