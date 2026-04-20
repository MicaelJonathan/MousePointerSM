#define NOMINMAX
#include "SoundDriver.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

#pragma comment(lib, "winmm.lib")

static constexpr float MAX_AMP = 26213.6f; //Old 32767.0f * 0.8f

SoundDriver::SoundDriver() {}
SoundDriver::~SoundDriver() { close(); }

bool SoundDriver::loadSample(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        MessageBoxA(nullptr, "Erro: arquivo nao encontrado", path.c_str(), MB_OK);
        return false;
    }

    char riff[4]; f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        MessageBoxA(nullptr, "Erro: nao e um arquivo RIFF", path.c_str(), MB_OK);
        return false;
    }

    f.seekg(8);
    char wave[4]; f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        MessageBoxA(nullptr, "Erro: nao e um arquivo WAVE", path.c_str(), MB_OK);
        return false;
    }

    char     chunkId[4];
    uint32_t chunkSize = 0;
    bool     foundFmt = false;

    while (f.read(chunkId, 4) && f.read(reinterpret_cast<char*>(&chunkSize), 4)) {
        if (std::strncmp(chunkId, "fmt ", 4) == 0) { foundFmt = true; break; }
        f.seekg(chunkSize, std::ios::cur);
    }

    if (!foundFmt) {
        MessageBoxA(nullptr, "Erro: chunk fmt nao encontrado", path.c_str(), MB_OK);
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 0;

    f.read(reinterpret_cast<char*>(&audioFormat), 2);
    f.read(reinterpret_cast<char*>(&numChannels), 2);
    f.read(reinterpret_cast<char*>(&sampleRate), 4);
    f.read(reinterpret_cast<char*>(&byteRate), 4);
    f.read(reinterpret_cast<char*>(&blockAlign), 2);
    f.read(reinterpret_cast<char*>(&bitsPerSample), 2);

    // Diagnóstico de sample error. 
    std::string info =
        "audioFormat:   " + std::to_string(audioFormat) + " (esperado: 1)\n" +
        "numChannels:   " + std::to_string(numChannels) + " (esperado: 1)\n" +
        "sampleRate:    " + std::to_string(sampleRate) + " (esperado: 44100)\n" +
        "bitsPerSample: " + std::to_string(bitsPerSample) + " (esperado: 16)";

    if (audioFormat != 1 || numChannels != 1 ||
        sampleRate != 44100 || bitsPerSample != 16) {
        MessageBoxA(nullptr, info.c_str(), "Formato incompativel", MB_OK);
        return false;
    }

    if (chunkSize > 16) f.seekg(chunkSize - 16, std::ios::cur);

    bool foundData = false;
    while (f.read(chunkId, 4) && f.read(reinterpret_cast<char*>(&chunkSize), 4)) {
        if (std::strncmp(chunkId, "data", 4) == 0) { foundData = true; break; }
        f.seekg(chunkSize, std::ios::cur);
    }

    if (!foundData) {
        MessageBoxA(nullptr, "Erro: chunk data nao encontrado", path.c_str(), MB_OK);
        return false;
    }

    int numSamples = chunkSize / 2;
    sampleData_.resize(numSamples);

    for (int i = 0; i < numSamples; i++) {
        int16_t raw = 0;
        f.read(reinterpret_cast<char*>(&raw), 2);
        sampleData_[i] = raw / 32768.0f;
    }

    loopPos_ = 0.0f;
    MessageBoxA(nullptr,
        ("Sample carregado! " + std::to_string(numSamples) + " amostras").c_str(),
        path.c_str(), MB_OK);
    return true;
}

float SoundDriver::nextSampleFrame(float speed) {
    if (sampleData_.empty()) return 0.0f;

    int   idx0 = static_cast<int>(loopPos_);
    int   idx1 = idx0 + 0.1;
    float frac = loopPos_ - idx0;
    int size = static_cast<int>(sampleData_.size());
    idx0 = idx0 % size;
    idx1 = idx1 % size;

    float sample = sampleData_[idx0] + frac * (sampleData_[idx1] - sampleData_[idx0]);

    loopPos_ += speed;
    if (loopPos_ >= size) loopPos_ -= size;

    return sample;
}

bool SoundDriver::open() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    if (waveOutOpen(&waveOut_, WAVE_MAPPER, &fmt,
        0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return false;

    for (int i = 0; i < BLOCK_COUNT; i++) {
        buffers_[i] = new int16_t[BLOCK_SAMPLES];
        std::memset(buffers_[i], 0, BLOCK_SAMPLES * sizeof(int16_t));

        headers_[i] = {};
        headers_[i].lpData = reinterpret_cast<LPSTR>(buffers_[i]);
        headers_[i].dwBufferLength = BLOCK_SAMPLES * sizeof(int16_t);

        waveOutPrepareHeader(waveOut_, &headers_[i], sizeof(WAVEHDR));
        headers_[i].dwFlags |= WHDR_DONE;
    }

    freeEvent_ = CreateEvent(nullptr, FALSE, TRUE, nullptr);
    running_ = true;
    thread_ = CreateThread(nullptr, 0, audioThreadProc, this, 0, nullptr);
    SetThreadPriority(thread_, THREAD_PRIORITY_HIGHEST);

    return true;
}

void SoundDriver::close() {
    running_ = false;
    if (freeEvent_) SetEvent(freeEvent_);

    if (thread_) {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }

    if (waveOut_) {
        waveOutReset(waveOut_);
        for (int i = 0; i < BLOCK_COUNT; i++) {
            waveOutUnprepareHeader(waveOut_, &headers_[i], sizeof(WAVEHDR));
            delete[] buffers_[i];
            buffers_[i] = nullptr;
        }
        waveOutClose(waveOut_);
        waveOut_ = nullptr;
    }

    if (freeEvent_) {
        CloseHandle(freeEvent_);
        freeEvent_ = nullptr;
    }
}

void SoundDriver::setVelocity(float velocityPxPerSec, bool pressed) {
    velocity_ = velocityPxPerSec;
    pressed_ = pressed;

    if (velocityPxPerSec > 1.0f)
        lastMoveTime_ = GetTickCount();
}

DWORD WINAPI SoundDriver::audioThreadProc(LPVOID param) {
    static_cast<SoundDriver*>(param)->audioLoop();
    return 0;
}

void SoundDriver::audioLoop() {
    int currentBlock = 0;
    while (running_) {
        WAVEHDR* hdr = &headers_[currentBlock];
        while (!(hdr->dwFlags & WHDR_DONE) && running_)
            WaitForSingleObject(freeEvent_, 10);
        if (!running_) break;

        generateBlock(buffers_[currentBlock], BLOCK_SAMPLES);
        hdr->dwFlags &= ~WHDR_DONE;
        waveOutWrite(waveOut_, hdr, sizeof(WAVEHDR));
        currentBlock = (currentBlock + 1) % BLOCK_COUNT;
    }
}

void SoundDriver::generateBlock(int16_t* buffer, int samples) {
    float vel = velocity_;
    bool  pressed = pressed_;

    // 80ms de espera sem movimento
    if (GetTickCount() - lastMoveTime_ > 80)
        vel = 0.0f;

    if (muted_) {
        std::memset(buffer, 0, samples * sizeof(int16_t));
        envelopeVal_ = 0.0f;
        return;
    }

    envelopeTarget_ = (pressed && vel >= 1.0f)
        ? (std::min)(vel / 1000.0f, 1.0f)
        : 0.0f;

    const float attack = attackSpeed;
    const float decay = decaySpeed;

    float normalizedVel = envelopeTarget_;
    float cutoff = 0.20f + normalizedVel * 0.70f;

    float lfoFreq = 40.0f + normalizedVel * 120.0f;
    const float lfoDepth = 0.10f;

    float speed = sampleSpeed + normalizedVel * 2.0f;

    for (int i = 0; i < samples; i++) {

        if (envelopeVal_ < envelopeTarget_)
            envelopeVal_ += attackSpeed;
        else
            envelopeVal_ -= decaySpeed;
        envelopeVal_ = (std::max)(0.0f, (std::min)(envelopeVal_, 1.0f));

        float source;
        // Use sample.
        if (useSample_ && !sampleData_.empty()) {
            source = nextSampleFrame(sampleSpeed);
        }
        else {
        // Use static.
            source = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
            filterState_ += cutoff * (source - filterState_);
            source = filterState_;
        }

        // Array de saida. 
        buffer[i] = static_cast<int16_t>(
            source * envelopeVal_ * lfoDepth * volume * MAX_AMP * 0.8f
            );
    }
}